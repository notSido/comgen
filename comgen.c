/* comgen - Natural language to bash command generator */
#include <curl/curl.h>
#include <pwd.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#define MAX_PROMPT 4096
#define MAX_RESPONSE 65536
#define MAX_CMD 8192
#define MAX_SYSTEM_PROMPT 65536
#define MAX_LS_OUTPUT 16384

/* ANSI colors */
#define C_RESET "\033[0m"
#define C_BOLD "\033[1m"
#define C_DIM "\033[2m"
#define C_RED "\033[31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_BLUE "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN "\033[36m"

static char response_buf[MAX_RESPONSE];
static size_t response_len;
static char system_prompt[MAX_SYSTEM_PROMPT];
static char ls_output[MAX_LS_OUTPUT];

/* Environment context */
typedef struct {
  char cwd[1024];
  char home[256];
  char user[64];
  char hostname[64];
  char os[256];
  char shell[128];
} EnvContext;

static EnvContext env_ctx;

static void gather_context(void) {
  /* Current working directory */
  if (!getcwd(env_ctx.cwd, sizeof(env_ctx.cwd)))
    strcpy(env_ctx.cwd, "unknown");

  /* Home directory */
  const char *home = getenv("HOME");
  if (home)
    strncpy(env_ctx.home, home, sizeof(env_ctx.home) - 1);
  else
    strcpy(env_ctx.home, "unknown");

  /* Username */
  struct passwd *pw = getpwuid(getuid());
  if (pw && pw->pw_name)
    strncpy(env_ctx.user, pw->pw_name, sizeof(env_ctx.user) - 1);
  else
    strcpy(env_ctx.user, "unknown");

  /* Hostname */
  if (gethostname(env_ctx.hostname, sizeof(env_ctx.hostname)) != 0)
    strcpy(env_ctx.hostname, "unknown");

  /* OS info */
  struct utsname uts;
  if (uname(&uts) == 0)
    snprintf(env_ctx.os, sizeof(env_ctx.os), "%s %s", uts.sysname, uts.release);
  else
    strcpy(env_ctx.os, "unknown");

  /* Shell */
  const char *shell = getenv("SHELL");
  if (shell)
    strncpy(env_ctx.shell, shell, sizeof(env_ctx.shell) - 1);
  else
    strcpy(env_ctx.shell, "/bin/bash");
}

static void build_system_prompt(void) {
  snprintf(system_prompt, sizeof(system_prompt),
           "You are a command-line assistant. Convert natural language to a "
           "single bash command. "
           "Output ONLY the command, no explanations or markdown. "
           "If impossible, respond with: ERROR: reason\n\n"
           "Environment:\n"
           "- OS: %s\n"
           "- Shell: %s\n"
           "- User: %s\n"
           "- Hostname: %s\n"
           "- Home: %s\n"
           "- CWD: %s",
           env_ctx.os, env_ctx.shell, env_ctx.user, env_ctx.hostname,
           env_ctx.home, env_ctx.cwd);

  if (ls_output[0] != '\0') {
    size_t current_len = strlen(system_prompt);
    snprintf(system_prompt + current_len, sizeof(system_prompt) - current_len,
             "\n\nFiles in current directory:\n%s", ls_output);
  }
}

static void capture_ls_output(void) {
  FILE *fp = popen("ls -la", "r");
  if (!fp) {
    printf(C_RED "Failed to run ls -la" C_RESET "\n");
    return;
  }

  size_t len = fread(ls_output, 1, sizeof(ls_output) - 1, fp);
  ls_output[len] = '\0';
  pclose(fp);

  printf(C_DIM "Captured directory listing (%zu bytes)" C_RESET "\n", len);
  build_system_prompt();
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data) {
  (void)data;
  size_t len = size * nmemb;
  if (response_len + len < MAX_RESPONSE - 1) {
    memcpy(response_buf + response_len, ptr, len);
    response_len += len;
    response_buf[response_len] = '\0';
  }
  return len;
}

/* Simple JSON string escape */
static void json_escape(const char *src, char *dst, size_t dstlen) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j < dstlen - 2; i++) {
    if (src[i] == '"' || src[i] == '\\')
      dst[j++] = '\\';
    else if (src[i] == '\n') {
      dst[j++] = '\\';
      dst[j++] = 'n';
      continue;
    } else if (src[i] == '\r') {
      dst[j++] = '\\';
      dst[j++] = 'r';
      continue;
    } else if (src[i] == '\t') {
      dst[j++] = '\\';
      dst[j++] = 't';
      continue;
    }
    dst[j++] = src[i];
  }
  dst[j] = '\0';
}

/* Extract text content from Claude API response */
static char *extract_content(const char *json) {
  const char *p = strstr(json, "\"text\":");
  if (!p)
    return NULL;
  p = strchr(p + 7, '"');
  if (!p)
    return NULL;
  p++;

  static char content[MAX_CMD];
  size_t i = 0;
  while (*p && *p != '"' && i < MAX_CMD - 1) {
    if (*p == '\\' && p[1]) {
      p++;
      if (*p == 'n')
        content[i++] = '\n';
      else if (*p == 't')
        content[i++] = '\t';
      else if (*p == 'r')
        content[i++] = '\r';
      else
        content[i++] = *p;
    } else {
      content[i++] = *p;
    }
    p++;
  }
  content[i] = '\0';
  return content;
}

static char *generate_command(const char *prompt, const char *api_key) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return NULL;

  char escaped_prompt[MAX_PROMPT * 2];
  char escaped_system[MAX_SYSTEM_PROMPT * 2];
  json_escape(prompt, escaped_prompt, sizeof(escaped_prompt));
  json_escape(system_prompt, escaped_system, sizeof(escaped_system));

  char *post_data = malloc(MAX_PROMPT * 3 + MAX_SYSTEM_PROMPT * 2);
  snprintf(post_data, MAX_PROMPT * 3 + MAX_SYSTEM_PROMPT * 2,
           "{\"model\":\"claude-sonnet-4-20250514\",\"max_tokens\":1024,"
           "\"system\":\"%s\","
           "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
           escaped_system, escaped_prompt);

  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, auth_header);
  headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

  response_len = 0;
  response_buf[0] = '\0';

  curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);

  CURLcode res = curl_easy_perform(curl);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  free(post_data);

  if (res != CURLE_OK) {
    fprintf(stderr, C_RED "Error: %s" C_RESET "\n", curl_easy_strerror(res));
    return NULL;
  }

  return extract_content(response_buf);
}

static int execute_command(const char *cmd) {
  printf("\n" C_DIM "Executing..." C_RESET "\n\n");
  int ret = system(cmd);
  printf("\n");
  if (WIFEXITED(ret)) {
    int code = WEXITSTATUS(ret);
    if (code == 0)
      printf(C_GREEN "✓ Success" C_RESET "\n");
    else
      printf(C_RED "✗ Exit code: %d" C_RESET "\n", code);
    return code;
  }
  return -1;
}

static void print_command(const char *cmd) {
  printf("\n" C_MAGENTA C_BOLD "Command:" C_RESET " %s\n", cmd);
}

static char prompt_action(void) {
  printf(C_DIM "Execute? " C_RESET "[" C_GREEN "y" C_RESET "/" C_RED "n" C_RESET
               "/" C_YELLOW "e" C_RESET "dit]: ");
  fflush(stdout);

  char buf[16];
  if (!fgets(buf, sizeof(buf), stdin))
    return 'n';

  char c = buf[0];
  if (c == '\n' || c == 'y' || c == 'Y')
    return 'y';
  if (c == 'e' || c == 'E')
    return 'e';
  return 'n';
}

int main(void) {
  const char *api_key = getenv("ANTHROPIC_API_KEY");
  if (!api_key) {
    fprintf(stderr, C_RED "Error: ANTHROPIC_API_KEY not set" C_RESET "\n");
    return 1;
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  using_history();

  /* Gather environment context */
  gather_context();
  build_system_prompt();

  printf(C_BOLD C_MAGENTA "comgen" C_RESET " - Natural language to bash\n");
  printf(C_DIM "Type /q to quit" C_RESET "\n\n");

  char *line;
  while ((line = readline(C_BLUE C_BOLD "comgen> " C_RESET)) != NULL) {
    if (!*line) {
      free(line);
      continue;
    }

    /* Commands */
    if (strcmp(line, "/q") == 0 || strcmp(line, "/quit") == 0) {
      free(line);
      break;
    }
    if (strcmp(line, "/h") == 0 || strcmp(line, "/help") == 0) {
      printf("Type natural language, get bash commands.\n");
      printf("/q quit  /h help\n");
      free(line);
      continue;
    }
    if (strcmp(line, "/ls") == 0) {
      capture_ls_output();
      free(line);
      continue;
    }

    add_history(line);

    /* Refresh cwd in case it changed */
    if (getcwd(env_ctx.cwd, sizeof(env_ctx.cwd)))
      build_system_prompt();

    printf(C_DIM "Thinking..." C_RESET "\r");
    fflush(stdout);

    char *cmd = generate_command(line, api_key);
    printf("             \r"); /* Clear "Thinking..." */

    if (!cmd) {
      printf(C_RED "Failed to generate command" C_RESET "\n");
      free(line);
      continue;
    }

    if (strncmp(cmd, "ERROR:", 6) == 0) {
      printf(C_RED "%s" C_RESET "\n", cmd);
      free(line);
      continue;
    }

    print_command(cmd);

    char action = prompt_action();

    if (action == 'y') {
      execute_command(cmd);
    } else if (action == 'e') {
      char *edited = readline(C_YELLOW "$ " C_RESET);
      if (edited && *edited) {
        execute_command(edited);
        free(edited);
      }
    } else {
      printf(C_DIM "Skipped" C_RESET "\n");
    }

    free(line);
  }

  printf(C_DIM "Goodbye!" C_RESET "\n");
  curl_global_cleanup();
  return 0;
}
