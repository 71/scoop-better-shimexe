#pragma comment(lib, "SHELL32.LIB")

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

#define MAX_FILENAME_SIZE 512

BOOL WINAPI ctrlhandler(DWORD fdwCtrlType )
{
  switch (fdwCtrlType) {
    // Ignore all events, and let the child process
    // handle them.
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      return TRUE;

    default:
      return FALSE;
  }
}

int compute_program_length(char* commandline)
{
  int i = 0;

  if (commandline[0] == '"') {
    // Wait till end of string
    i++;

    for (;;) {
      char c = commandline[i++];

      if (c == 0)
        return i - 1;
      else if (c == '\\')
        i++;
      else if (c == '"')
        return i;
    }
  } else {
    for (;;) {
      char c = commandline[i++];

      if (c == 0)
        return i - 1;
      else if (c == '\\')
        i++;
      else if (c == ' ')
        return i;
    }
  }
}

int main()
{
  // Find filename of current executable.
  char filename[MAX_FILENAME_SIZE];
  unsigned long filename_size = GetModuleFileNameA(NULL, filename, MAX_FILENAME_SIZE);

  if (filename_size == MAX_FILENAME_SIZE) {
    fprintf(stderr, "The filename of the program is too long to handle.\n");

    return 1;
  }

  // Use filename of current executable to find .shim
  filename[filename_size - 3] = 's';
  filename[filename_size - 2] = 'h';
  filename[filename_size - 1] = 'i';
  filename[filename_size - 0] = 'm';
  filename[filename_size + 1] =  0 ;

  FILE* shim_file = fopen(filename, "r");

  if (shim_file == NULL) {
    fprintf(stderr, "Cannot open shim file for read.\n");

    return 1;
  }

  size_t command_length = 256;
  size_t path_length;
  size_t args_length;

  // Read shim
  char* path = NULL;
  char* args = NULL;
  char* linebuf = calloc(20000, sizeof(char));

  for (;;) {
    char* line = fgets(linebuf, 20000, shim_file);

    if (line == NULL)
      break;

    if (line[0] == (char)0xEF && line[1] == (char)0xBB && line[2] == (char)0xBF)
      // Damn BOM
      line += 3;

    if (line[4] != ' ' || line[5] != '=' || line[6] != ' ')
      continue;

    const int len = strlen(line) - 8;

    if (line[0] == 'p' && line[1] == 'a' && line[2] == 't' && line[3] == 'h') {
      // Reading path
      path = malloc(len);
      strncpy(path, line + 7, len);
      path[len] = 0;

      command_length += len;
      path_length = len;

      continue;
    }

    if (line[0] == 'a' && line[1] == 'r' && line[2] == 'g' && line[3] == 's') {
      // Reading args
      args = malloc(len);
      strncpy(args, line + 7, len);
      args[len] = 0;

      command_length += len;
      args_length = len;

      continue;
    }

    continue;
  }

  fclose(shim_file);

  if (path == NULL) {
    fprintf(stderr, "Could not read shim file.\n");

    return 1;
  }

  // Find length of command to run
  char* given_cmd = GetCommandLineA();
  int given_length = strlen(given_cmd);

  command_length += given_length;

  // Start building command to run, using '[path] [args]', as given by shim.
  char* cmd = (char*)malloc(command_length);
  int cmd_i = 0;

  strcpy(cmd, path);
  cmd[path_length] = ' ';
  cmd_i += path_length + 1;

  if (args != NULL) {
    strcpy(cmd + path_length + 1, args);
    cmd[path_length + args_length] = ' ';
    cmd_i += args_length + 1;
  }

  // Copy all given arguments to command
  int program_length = compute_program_length(given_cmd);

  strcpy(cmd + cmd_i, given_cmd + program_length);

  // Start subprocess
  STARTUPINFO si = {0};
  PROCESS_INFORMATION pi = {0};

  if (!CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
    if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
      // We must elevate the process, which is (basically) impossible with
      // CreateProcess, and therefore we fallback to ShellExecuteEx,
      // which CAN create elevated processes, at the cost of opening a new separate
      // window.
      // Theorically, this could be fixed (or rather, worked around) using pipes
      // and IPC, but... this is a question for another day.
      SHELLEXECUTEINFOA sei = {0};

      sei.cbSize       = sizeof(SHELLEXECUTEINFOA);
      sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
      sei.lpFile       = path;
      sei.lpParameters = cmd + path_length + 1;
      sei.nShow        = SW_SHOW;

      if (!ShellExecuteExA(&sei)) {
        fprintf(stderr, "Unable to create elevated process: error %i.", GetLastError());

        return 1;
      }

      pi.hProcess = sei.hProcess;
    } else {
      fprintf(stderr, "Could not create process with command '%s'.\n", cmd);

      return 1;
    }
  }

  // Free obsolete buffers
  free(path);
  free(args);
  free(linebuf);
  free(cmd);

  // Ignore Ctrl-C and other signals
  if (!SetConsoleCtrlHandler(ctrlhandler, TRUE))
    fprintf(stderr, "Could not set control handler; Ctrl-C behavior may be invalid.\n");

  // Wait till end of process
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  // Dispose of everything
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return (int)exit_code;
}
