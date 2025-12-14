"""Command execution module."""

import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass
class ExecutionResult:
    """Result of command execution."""

    command: str
    return_code: int
    stdout: str
    stderr: str

    @property
    def success(self) -> bool:
        return self.return_code == 0

    @property
    def output(self) -> str:
        """Combined output (stdout + stderr)."""
        parts = []
        if self.stdout:
            parts.append(self.stdout)
        if self.stderr:
            parts.append(self.stderr)
        return "\n".join(parts)


class CommandExecutor:
    """Executes bash commands safely."""

    def __init__(self, shell: str = "/bin/bash", working_dir: Path | None = None):
        self.shell = shell
        self.working_dir = working_dir or Path.cwd()

    def execute(self, command: str, timeout: int = 300) -> ExecutionResult:
        """
        Execute a bash command.

        Args:
            command: The command to execute
            timeout: Maximum execution time in seconds (default 5 minutes)

        Returns:
            ExecutionResult with command output and status
        """
        try:
            result = subprocess.run(
                command,
                shell=True,
                executable=self.shell,
                cwd=self.working_dir,
                capture_output=True,
                text=True,
                timeout=timeout,
            )

            return ExecutionResult(
                command=command,
                return_code=result.returncode,
                stdout=result.stdout,
                stderr=result.stderr,
            )

        except subprocess.TimeoutExpired:
            return ExecutionResult(
                command=command,
                return_code=-1,
                stdout="",
                stderr=f"Command timed out after {timeout} seconds",
            )
        except Exception as e:
            return ExecutionResult(
                command=command,
                return_code=-1,
                stdout="",
                stderr=f"Execution error: {e}",
            )

    def update_working_dir(self, new_dir: Path):
        """Update the working directory for command execution."""
        if new_dir.is_dir():
            self.working_dir = new_dir
