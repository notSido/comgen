/* comgen - Natural language to bash command generator */
#ifdef _WIN32
#include <lmcons.h>
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#include <pwd.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Constants */
#define DEFAULT_MODEL "claude-sonnet-4-20250514"
#define MAX_RESPONSE_CHUNK 4096

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

/* Session State */
typedef struct {
  char *api_key;
  char *model;
#ifdef _WIN32
  HINTERNET hSession;
  HINTERNET hConnect;
#else
  CURL *curl;
#endif
} ComgenSession;

/* Dynamic String Buffer */
typedef struct {
  char *data;
  size_t len;
  size_t cap;
} StringBuffer;

static void sb_init(StringBuffer *sb) {
  sb->cap = 1024;
  sb->len = 0;
  sb->data = malloc(sb->cap);
  if (sb->data)
    sb->data[0] = '\0';
}

static void sb_free(StringBuffer *sb) {
  free(sb->data);
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static void sb_append(StringBuffer *sb, const char *str) {
  if (!str)
    return;
  size_t len = strlen(str);
  if (sb->len + len >= sb->cap) {
    size_t new_cap = sb->cap * 2 + len;
    char *new_data = realloc(sb->data, new_cap);
    if (!new_data)
      return; /* OOM handling simplified */
    sb->data = new_data;
    sb->cap = new_cap;
  }
  memcpy(sb->data + sb->len, str, len);
  sb->len += len;
  sb->data[sb->len] = '\0';
}

/* Global Context - managed dynamically now, but kept in struct for organization
 */
typedef struct {
  char cwd[1024];
  char user[64];
  char os[256];
  char shell[128];
  char *ls_output; /* Dynamic */
} EnvContext;

static EnvContext env_ctx;

static void gather_context(void) {
#ifdef _WIN32
  if (!GetCurrentDirectory(sizeof(env_ctx.cwd), env_ctx.cwd))
    strcpy(env_ctx.cwd, ".");

  DWORD len = sizeof(env_ctx.user);
  if (!GetUserName(env_ctx.user, &len))
    strcpy(env_ctx.user, "user");

  strcpy(env_ctx.os, "Win"); // Compact

  const char *comspec = getenv("COMSPEC");
  strncpy(env_ctx.shell, comspec ? comspec : "cmd", sizeof(env_ctx.shell) - 1);
#else
  if (!getcwd(env_ctx.cwd, sizeof(env_ctx.cwd)))
    strcpy(env_ctx.cwd, ".");

  struct passwd *pw = getpwuid(getuid());
  strncpy(env_ctx.user, pw ? pw->pw_name : "user", sizeof(env_ctx.user) - 1);

  struct utsname uts;
  if (uname(&uts) == 0)
    snprintf(env_ctx.os, sizeof(env_ctx.os), "%s", uts.sysname);
  else
    strcpy(env_ctx.os, "Linux");

  const char *shell = getenv("SHELL");
  /* Extract just the shell name for brevity (e.g. /bin/bash -> bash) */
  if (shell) {
    char *p = strrchr(shell, '/');
    strncpy(env_ctx.shell, p ? p + 1 : shell, sizeof(env_ctx.shell) - 1);
  } else {
    strcpy(env_ctx.shell, "bash");
  }
#endif
}

/* Optimized Prompt Building */
static char *build_system_prompt(void) {
  StringBuffer sb;
  sb_init(&sb);

  /* Golfed System Prompt: ~60-70 tokens base */
  sb_append(
      &sb,
      "Task:Natural language->Bash command.Rules:NO markdown/explanation.ONLY "
      "command text.Failure:\"ERROR:reason\".Ctx:");

  char buf[2048];
  snprintf(buf, sizeof(buf), "OS:%s|Shell:%s|User:%s|CWD:%s", env_ctx.os,
           env_ctx.shell, env_ctx.user, env_ctx.cwd);
  sb_append(&sb, buf);

  if (env_ctx.ls_output) {
    sb_append(&sb, "|Files:");
    sb_append(&sb, env_ctx.ls_output);
  }

  return sb.data; /* Caller must free */
}

static void capture_ls_output(void) {
  if (env_ctx.ls_output)
    free(env_ctx.ls_output);

  StringBuffer sb;
  sb_init(&sb);

#ifdef _WIN32
  FILE *fp = _popen("dir /B /A-D", "r"); /* Files only, bare format */
#else
  /* Compact list: names only, one per line (efficient for tokenizer than
   * columnar) */
  /* Using ls -p to mark dirs with /, grep -v / to filter dirs? No, let's keep
     it simple. ls -1A = one column, almost all. */
  FILE *fp = popen("ls -1A", "r");
#endif
  if (!fp) {
    printf(C_RED "ls failed" C_RESET "\n");
    return;
  }

  char chunk[1024];
  size_t count = 0;
  /* Limit capture to avoid blowing up context */
  while (fgets(chunk, sizeof(chunk), fp) && count < 50) {
    /* Replace newline with comma for token efficiency?
       Actually newlines are fine, but comma-space is often slightly fewer
       tokens for lists in LLMs? Let's stick to simple list but maybe trim
       whitespace. */
    size_t len = strlen(chunk);
    if (len > 0 && chunk[len - 1] == '\n')
      chunk[len - 1] = '\0';
    if (count > 0)
      sb_append(&sb, ",");
    sb_append(&sb, chunk);
    count++;
  }
  if (count >= 50)
    sb_append(&sb, ",...");

#ifdef _WIN32
  _pclose(fp);
#else
  pclose(fp);
#endif

  env_ctx.ls_output = sb.data;
  printf(C_DIM "Captured file list (%zu chars)" C_RESET "\n", sb.len);
}

/* JSON Escape & Utils */
static char *json_escape(const char *src) {
  if (!src)
    return strdup("");
  StringBuffer sb;
  sb_init(&sb);

  for (const char *p = src; *p; p++) {
    if (*p == '"' || *p == '\\') {
      sb_append(&sb, "\\");
      char c[2] = {*p, 0};
      sb_append(&sb, c);
    } else if (*p == '\n') {
      sb_append(&sb, "\\n");
    } else if (*p == '\r') {
      sb_append(&sb, "\\r");
    } else if (*p == '\t') {
      sb_append(&sb, "\\t");
    } else {
      char c[2] = {*p, 0};
      sb_append(&sb, c);
    }
  }
  return sb.data;
}

static char *extract_content(const char *json) {
  const char *marker = "\"text\":";
  const char *p = strstr(json, marker);
  if (!p)
    return NULL;

  p = strchr(p + strlen(marker), '"');
  if (!p)
    return NULL;
  p++; /* Skip opening quote */

  StringBuffer sb;
  sb_init(&sb);

  while (*p) {
    if (*p == '"' && *(p - 1) != '\\')
      break; /* End of string */

    if (*p == '\\') {
      p++;
      if (*p == 'n')
        sb_append(&sb, "\n");
      else if (*p == 't')
        sb_append(&sb, "\t");
      else if (*p == 'r')
        sb_append(&sb, "\r");
      else if (*p) {
        char c[2] = {*p, 0};
        sb_append(&sb, c);
      }
    } else {
      char c[2] = {*p, 0};
      sb_append(&sb, c);
    }
    p++;
  }
  return sb.data;
}

static void print_token_usage(const char *json) {
  const char *p = strstr(json, "input_tokens\":");
  int input = 0, output = 0;
  if (p)
    input = atoi(p + 14);

  p = strstr(json, "output_tokens\":");
  if (p)
    output = atoi(p + 15);

  printf(C_DIM "Tokens: %d in, %d out" C_RESET "\n", input, output);
}

/* Network Logic */
#ifndef _WIN32
struct WriteContext {
  char *data;
  size_t len;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data) {
  size_t realsize = size * nmemb;
  StringBuffer *sb = (StringBuffer *)data;
  sb_append(sb,
            (char *)ptr); /* sb logic needs null termination which is fine */
  /* Actually sb_append copies and null terminates.
     Warning: not optimal for huge binary data but fine for JSON text */
  return realsize;
}
#endif

static char *generate_command(ComgenSession *session, const char *prompt) {
  char *sys_prompt = build_system_prompt();
  char *esc_sys = json_escape(sys_prompt);
  char *esc_prompt = json_escape(prompt);

  StringBuffer body;
  sb_init(&body);

  /* Construct JSON body */
  char head[512];
  snprintf(head, sizeof(head),
           "{\"model\":\"%s\",\"max_tokens\":1024,\"system\":\"%s\","
           "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
           session->model, esc_sys, esc_prompt);

  sb_append(&body, head);

  free(sys_prompt);
  free(esc_sys);
  free(esc_prompt);

  StringBuffer response;
  sb_init(&response);

#ifdef _WIN32
  HINTERNET hRequest = WinHttpOpenRequest(
      session->hConnect, L"POST", L"/v1/messages", NULL, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

  if (!hRequest) {
    sb_free(&body);
    sb_free(&response);
    return NULL;
  }

  wchar_t headers[1024];
  swprintf(headers, 1024,
           L"Content-Type: application/json\r\n"
           L"x-api-key: %S\r\n"
           L"anthropic-version: 2023-06-01",
           session->api_key);

  BOOL bResults = WinHttpSendRequest(hRequest, headers, -1L, body.data,
                                     (DWORD)body.len, (DWORD)body.len, 0);

  if (bResults)
    bResults = WinHttpReceiveResponse(hRequest, NULL);

  if (bResults) {
    DWORD dwSize = 0, dwDownloaded = 0;
    do {
      dwSize = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
        break;
      if (!dwSize)
        break;

      char *temp = malloc(dwSize + 1);
      if (temp) {
        ZeroMemory(temp, dwSize + 1);
        if (WinHttpReadData(hRequest, temp, dwSize, &dwDownloaded)) {
          sb_append(&response, temp); /* Append handles realloc */
        }
        free(temp);
      }
    } while (dwSize > 0);
  } else {
    printf(C_RED "WinHttp Error: %lu" C_RESET "\n", GetLastError());
  }
  WinHttpCloseHandle(hRequest);

#else
  curl_easy_setopt(session->curl, CURLOPT_POSTFIELDS, body.data);
  curl_easy_setopt(session->curl, CURLOPT_WRITEDATA, &response);

  /* Reset buffer logic for curl callback */
  /* Re-using write_cb means we assume response is empty initially */

  CURLcode res = curl_easy_perform(session->curl);
  if (res != CURLE_OK) {
    fprintf(stderr, C_RED "CURL Error: %s" C_RESET "\n",
            curl_easy_strerror(res));
  }
#endif

  sb_free(&body);

  if (response.len == 0) {
    sb_free(&response);
    return NULL;
  }

  print_token_usage(response.data);
  char *content = extract_content(response.data);
  sb_free(&response);
  return content;
}

/* Initialization & Cleanup */
static int session_init(ComgenSession *s) {
  s->api_key = getenv("ANTHROPIC_API_KEY");
  if (!s->api_key)
    return 0;

  s->model = getenv("COMGEN_MODEL");
  if (!s->model)
    s->model = DEFAULT_MODEL;

#ifdef _WIN32
  s->hSession = WinHttpOpen(L"comgen/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s->hSession)
    return 0;

  s->hConnect = WinHttpConnect(s->hSession, L"api.anthropic.com",
                               INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!s->hConnect) {
    WinHttpCloseHandle(s->hSession);
    return 0;
  }
#else
  s->curl = curl_easy_init();
  if (!s->curl)
    return 0;

  struct curl_slist *headers = NULL;
  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", s->api_key);

  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, auth_header);
  headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

  curl_easy_setopt(s->curl, CURLOPT_URL,
                   "https://api.anthropic.com/v1/messages");
  curl_easy_setopt(
      s->curl, CURLOPT_HTTPHEADER,
      headers); /* Headers stored in handle? No, must keep list alive? */
  /* Actually CURLOPT_HTTPHEADER pointer must be valid during perform.
     Optimisation: We can create headers once and store in session, or
     re-create. Let's store them in session to be perfectly safe and efficient.
   */
  /* Simplified: Set them here, but we need to keep headers valid. */
  /* Fix: Add headers to struct or make static logic. For now, we will NOT free
     headers until exit if we set them here? CURL documentation says: "The
     linked list must be kept available until the transfer is done" So we can
     attach them to the handle permanently? No, easy_perform uses them.
     Optimization solution: Attached headers once in init. */

  /* Hack: We need to store persistent headers pointer in session to free it
     later, but ComgenSession definition is minimal. Let's just set it and
     forget strict purity or add `struct curl_slist *headers` to ComgenSession.
   */
  /* For this generated code, I'll attach it and let OS cleanup at exit,
     or ideally add void* internal_data to session. */

  /* Let's modify the struct locally if needed, or re-alloc headers per request
     (small overhead). Actually, re-alloc headers per request is safer if we
     don't change struct definition too much. BUT we want optimizations! Let's
     rely on the fact we only start curl once. */

  /* Wait, I can't modify the struct definition inside this function easily
     without forward declaration. Let's just attach headers once and keep them.
     Mem leak on exit is acceptable for CLI tool OS reclamation. */
  curl_easy_setopt(s->curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(s->curl, CURLOPT_WRITEFUNCTION, write_cb);
#endif
  return 1;
}

static void session_cleanup(ComgenSession *s) {
#ifdef _WIN32
  if (s->hConnect)
    WinHttpCloseHandle(s->hConnect);
  if (s->hSession)
    WinHttpCloseHandle(s->hSession);
#else
  if (s->curl)
    curl_easy_cleanup(s->curl);
#endif
}

static void execute_command(const char *cmd) {
  printf("\n" C_DIM "Executing..." C_RESET "\n");
  int ret = system(cmd);
#ifdef _WIN32
  if (ret == 0)
    printf(C_GREEN "Success" C_RESET "\n");
  else
    printf(C_RED "Exit: %d" C_RESET "\n", ret);
#else
  if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0)
    printf(C_GREEN "Success" C_RESET "\n");
  else
    printf(C_RED "Exit: %d" C_RESET "\n", WEXITSTATUS(ret));
#endif
  printf("\n");
}

/* Prompt Action */
static char prompt_action(void) {
  printf(C_BOLD "Execute? " C_RESET "[" C_GREEN "y" C_RESET "/" C_RED
                "n" C_RESET "/" C_YELLOW "e" C_RESET "dit]: ");
  fflush(stdout);

  char buf[64];
  if (!fgets(buf, sizeof(buf), stdin))
    return 'n';
  if (buf[0] == 'y' || buf[0] == 'Y' || buf[0] == '\n')
    return 'y';
  if (buf[0] == 'e' || buf[0] == 'E')
    return 'e';
  return 'n';
}

int main(void) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#else
  curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

  ComgenSession session = {0};
  if (!session_init(&session)) {
    fprintf(stderr, C_RED "Init failed. Check ANTHROPIC_API_KEY." C_RESET "\n");
    return 1;
  }

  gather_context();

  printf(C_MAGENTA C_BOLD "comgen 2.0" C_RESET " (%s)\n", session.model);
  printf(C_DIM "Ready. /q:quit /ls:scan files" C_RESET "\n\n");

  char *line_buf;
#ifdef _WIN32
  char win_buf[4096];
#endif

  while (1) {
#ifdef _WIN32
    printf(C_BLUE C_BOLD "comgen> " C_RESET);
    if (!fgets(win_buf, sizeof(win_buf), stdin))
      break;
    win_buf[strcspn(win_buf, "\r\n")] = 0;
    line_buf = strdup(win_buf);
#else
    line_buf = readline(C_BLUE C_BOLD "comgen> " C_RESET);
    if (!line_buf)
      break;
    if (*line_buf)
      add_history(line_buf);
#endif

    if (strcmp(line_buf, "/q") == 0) {
      free(line_buf);
      break;
    }
    if (strcmp(line_buf, "/ls") == 0) {
      capture_ls_output();
      free(line_buf);
      continue;
    }

    if (strlen(line_buf) > 0) {
      /* Refresh CWD context cheaply */
#ifdef _WIN32
      GetCurrentDirectory(sizeof(env_ctx.cwd), env_ctx.cwd);
#else
      getcwd(env_ctx.cwd, sizeof(env_ctx.cwd));
#endif

      printf(C_DIM "Thinking..." C_RESET "\r");
      fflush(stdout);

      char *cmd = generate_command(&session, line_buf);
      printf("             \r");

      if (cmd) {
        if (strncmp(cmd, "ERROR:", 6) == 0) {
          printf(C_RED "%s" C_RESET "\n", cmd);
        } else {
          printf("\n" C_MAGENTA "%s" C_RESET "\n", cmd);
          char action = prompt_action();
          if (action == 'y')
            execute_command(cmd);
          else if (action == 'e') {
            /* Simple edit flow */
            printf(C_YELLOW "Enter command: " C_RESET);
            char edit_buf[4096];
            if (fgets(edit_buf, sizeof(edit_buf), stdin)) {
#ifndef _WIN32
              edit_buf[strcspn(edit_buf, "\n")] = 0;
              execute_command(edit_buf);
#else
              execute_command(edit_buf); /* win fgets includes newline often
                                            handled by execute */
#endif
            }
          }
        }
        free(cmd);
      } else {
        printf(C_RED "Error generating command" C_RESET "\n");
      }
    }

    free(line_buf);
  }

  session_cleanup(&session);
#ifndef _WIN32
  curl_global_cleanup();
#endif
  return 0;
}
