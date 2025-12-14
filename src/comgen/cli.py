"""Main CLI interface for comgen."""

import sys
from pathlib import Path

import click
from prompt_toolkit import PromptSession
from prompt_toolkit.history import FileHistory
from prompt_toolkit.auto_suggest import AutoSuggestFromHistory
from prompt_toolkit.styles import Style
from rich.console import Console
from rich.panel import Panel
from rich.syntax import Syntax
from rich.text import Text
from rich.theme import Theme

from .config import load_config
from .executor import CommandExecutor
from .llm import CommandGenerator

# Custom theme for rich output
THEME = Theme(
    {
        "info": "cyan",
        "warning": "yellow",
        "error": "bold red",
        "success": "bold green",
        "command": "bold magenta",
        "prompt": "bold blue",
    }
)

# Style for prompt_toolkit
PROMPT_STYLE = Style.from_dict(
    {
        "prompt": "#6C71C4 bold",
        "": "#839496",
    }
)


class ComgenCLI:
    """Main CLI application class."""

    def __init__(self, config=None):
        self.config = config or load_config()
        self.console = Console(theme=THEME)
        self.executor = CommandExecutor(
            shell=self.config.shell, working_dir=self.config.working_dir
        )
        self.generator: CommandGenerator | None = None
        self.session: PromptSession | None = None

    def _init_generator(self) -> bool:
        """Initialize the command generator. Returns True if successful."""
        errors = self.config.validate()
        if errors:
            for error in errors:
                self.console.print(f"[error]Error:[/error] {error}")
            return False

        self.generator = CommandGenerator(self.config)
        return True

    def _init_prompt_session(self):
        """Initialize the prompt session with history."""
        history = FileHistory(str(self.config.history_file))
        self.session = PromptSession(
            history=history,
            auto_suggest=AutoSuggestFromHistory(),
            style=PROMPT_STYLE,
        )

    def print_banner(self):
        """Print the welcome banner."""
        banner = Text()
        banner.append("comgen", style="bold magenta")
        banner.append(" - ", style="dim")
        banner.append("Natural Language to Bash Commands", style="italic")

        self.console.print()
        self.console.print(Panel(banner, border_style="blue"))
        self.console.print()
        self.console.print("[dim]Type your request in natural language.[/dim]")
        self.console.print("[dim]Commands: /help, /clear, /quit[/dim]")
        self.console.print()

    def print_help(self):
        """Print help message."""
        help_text = """
[bold]Available Commands:[/bold]

  [cyan]/help[/cyan]     Show this help message
  [cyan]/clear[/cyan]    Clear conversation history
  [cyan]/quit[/cyan]     Exit comgen (also: /exit, Ctrl+D)

[bold]Usage:[/bold]

  Just type what you want to do in plain English:

  [dim]> list all python files modified in the last week[/dim]
  [dim]> find and replace 'foo' with 'bar' in all .txt files[/dim]
  [dim]> show disk usage sorted by size[/dim]

[bold]Execution Flow:[/bold]

  1. Type your request
  2. Review the generated command
  3. Choose: [green]y[/green]=execute, [red]n[/red]=skip, [yellow]e[/yellow]=edit
"""
        self.console.print(help_text)

    def handle_command(self, user_input: str) -> bool:
        """
        Handle slash commands.
        Returns True if a command was handled, False otherwise.
        """
        cmd = user_input.strip().lower()

        if cmd in ("/quit", "/exit"):
            self.console.print("\n[dim]Goodbye![/dim]")
            sys.exit(0)

        if cmd == "/help":
            self.print_help()
            return True

        if cmd == "/clear":
            if self.generator:
                self.generator.clear_history()
            self.console.print("[info]Conversation history cleared.[/info]")
            return True

        return False

    def prompt_for_action(self, command: str) -> str:
        """
        Prompt user for action on generated command.
        Returns: 'execute', 'skip', 'edit', or the edited command.
        """
        self.console.print()
        self.console.print("[dim]Execute this command?[/dim]")
        self.console.print(
            "[green]y[/green]=yes  [red]n[/red]=no  [yellow]e[/yellow]=edit"
        )

        while True:
            try:
                choice = self.session.prompt("Choice: ").strip().lower()

                if choice in ("y", "yes", ""):
                    return "execute"
                elif choice in ("n", "no"):
                    return "skip"
                elif choice in ("e", "edit"):
                    self.console.print("[dim]Edit the command (press Enter when done):[/dim]")
                    edited = self.session.prompt("$ ", default=command)
                    return edited.strip()
                else:
                    self.console.print("[warning]Please enter y, n, or e[/warning]")

            except (KeyboardInterrupt, EOFError):
                return "skip"

    def display_command(self, command: str):
        """Display the generated command with syntax highlighting."""
        self.console.print()
        syntax = Syntax(command, "bash", theme="monokai", word_wrap=True)
        self.console.print(Panel(syntax, title="Generated Command", border_style="magenta"))

    def display_result(self, result):
        """Display command execution result."""
        if result.success:
            title = "✓ Success"
            border_style = "green"
        else:
            title = f"✗ Failed (exit code: {result.return_code})"
            border_style = "red"

        output = result.output.strip() if result.output else "[dim]No output[/dim]"

        self.console.print()
        self.console.print(Panel(output, title=title, border_style=border_style))

    def process_request(self, user_input: str):
        """Process a natural language request."""
        # Generate command
        with self.console.status("[bold blue]Thinking..."):
            result = self.generator.generate(user_input)

        if result.is_error:
            self.console.print(f"\n[error]Error:[/error] {result.error}")
            return

        # Display generated command
        self.display_command(result.command)

        # Get user action
        action = self.prompt_for_action(result.command)

        if action == "skip":
            self.console.print("[dim]Command skipped.[/dim]")
            return

        # Determine which command to execute
        command_to_run = result.command if action == "execute" else action

        # Execute the command
        with self.console.status("[bold blue]Executing..."):
            exec_result = self.executor.execute(command_to_run)

        # Display result
        self.display_result(exec_result)

        # Add execution context for follow-up questions
        self.generator.add_execution_result(
            command_to_run, exec_result.output, exec_result.success
        )

    def run(self):
        """Main application loop."""
        self.print_banner()

        if not self._init_generator():
            sys.exit(1)

        self._init_prompt_session()

        while True:
            try:
                user_input = self.session.prompt(
                    [("class:prompt", "comgen> ")],
                ).strip()

                if not user_input:
                    continue

                # Handle slash commands
                if user_input.startswith("/"):
                    self.handle_command(user_input)
                    continue

                # Process natural language request
                self.process_request(user_input)

            except KeyboardInterrupt:
                self.console.print()  # New line after ^C
                continue

            except EOFError:
                self.console.print("\n[dim]Goodbye![/dim]")
                break


@click.command()
@click.option(
    "--model",
    "-m",
    default=None,
    help="Model to use (default: claude-sonnet-4-20250514)",
)
@click.option(
    "--working-dir",
    "-w",
    type=click.Path(exists=True, file_okay=False, path_type=Path),
    default=None,
    help="Working directory for command execution",
)
@click.version_option()
def main(model: str | None, working_dir: Path | None):
    """comgen - Generate bash commands from natural language using LLMs."""
    config = load_config()

    if model:
        config.model = model
    if working_dir:
        config.working_dir = working_dir

    cli = ComgenCLI(config)
    cli.run()


if __name__ == "__main__":
    main()
