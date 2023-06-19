#pragma comment(lib, "SHELL32.LIB")

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <Windows.h>

#ifndef ERROR_ELEVATION_REQUIRED
#  define ERROR_ELEVATION_REQUIRED 740
#endif

#define MAX_FILENAME_SIZE 512

BOOL WINAPI ctrlhandler(DWORD fdwCtrlType)
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

int compute_program_length(const wchar_t* commandline)
{
    int i = 0;

    if (commandline[0] == L'"') {
        // Wait till end of string
        i++;

        for (;;) {
            const wchar_t c = commandline[i++];

            if (c == 0)
                return i - 1;
            else if (c == L'\\')
                i++;
            else if (c == L'"')
                return i;
        }
    } else {
        for (;;) {
            const wchar_t c = commandline[i++];

            if (c == 0)
                return i - 1;
            else if (c == L'\\')
                i++;
            else if (c == L' ')
                return i;
        }
    }
}

wchar_t* build_parameters(wchar_t* shim_args)
{
    // Find length of command to run
    wchar_t* cmd_line = GetCommandLineW();

    cmd_line += compute_program_length(cmd_line);
    while (*cmd_line == L' ')
        ++cmd_line;

    const size_t cmd_line_args_len = wcslen(cmd_line);

    size_t params_len = 1;
    if (shim_args) {
        params_len += wcslen(shim_args);
        if (cmd_line_args_len)
            ++params_len; // space separator
    }
    params_len += cmd_line_args_len;

    wchar_t* params = calloc(params_len, sizeof(wchar_t));
    assert(params);

    if (shim_args) {
        wcscat_s(params, params_len, shim_args);
        if (cmd_line_args_len)
            wcscat_s(params, params_len, L" ");
    }

    wcscat_s(params, params_len, cmd_line);
    return params;
}

int main()
{
    DWORD exit_code = 0;

    wchar_t* path = NULL;
    wchar_t* args = NULL;
    wchar_t* params = NULL;

    // Find filename of current executable.
    wchar_t filename[MAX_FILENAME_SIZE + 2];
    const unsigned int filename_size = GetModuleFileNameW(NULL, filename, MAX_FILENAME_SIZE);

    if (filename_size >= MAX_FILENAME_SIZE) {
        fprintf(stderr, "The filename of the program is too long to handle.\n");

        exit_code = 1;
        goto cleanup;
    }

    // Use filename of current executable to find .shim
    filename[filename_size - 3] = L's';
    filename[filename_size - 2] = L'h';
    filename[filename_size - 1] = L'i';
    filename[filename_size - 0] = L'm';
    filename[filename_size + 1] = 0;

    FILE* shim_file;

    if ((shim_file = _wfsopen(filename, L"r,ccs=UTF-8", _SH_DENYNO)) == NULL) {
        fprintf(stderr, "Cannot open shim file for read.\n");

        exit_code = 1;
        goto cleanup;
    }

    wchar_t* const line_buf = calloc(8192, sizeof(wchar_t));

    // Read shim
    for (;;) {
        wchar_t* line = fgetws(line_buf, 8192, shim_file);

        if (line == NULL)
            break;

        size_t line_len = wcslen(line);

        // trim trailing white space including CR & LF
        while (line_len > 7 && iswspace(line[line_len - 1]))
            --line_len;
        line[line_len] = 0;

        size_t offset = 7;
        size_t len = line_len - offset;

        if (!wmemcmp(line, L"path = ", 7)) {
            // Reading path

            // skip quotes
            if (line[offset] == L'"' && line[line_len - 1] == L'"') {
                ++offset;
                len -= 2;
            }

            path = calloc(len + 1, sizeof(wchar_t));
            assert(path);
            wmemcpy(path, line + offset, len);
        }
        else if (!wmemcmp(line, L"args = ", 7)) {
            // Reading args
            args = calloc(len + 1, sizeof(wchar_t));
            assert(args);
            wmemcpy(args, line + offset, len);
        }
    }

    free(line_buf);
    fclose(shim_file);

    if (path == NULL) {
        fprintf(stderr, "Could not read shim file.\n");

        exit_code = 1;
        goto cleanup;
    }

    params = build_parameters(args);

    // Find out if the target program is a console app
    SHFILEINFOW sfi = {0};
    const BOOL is_windows_app = HIWORD(SHGetFileInfoW(path, -1, &sfi, sizeof(sfi), SHGFI_EXETYPE));

    HANDLE job_handle = NULL;

    if (is_windows_app) {
        // Unfortunately, this technique will still show a window for a fraction of time,
        // but there's just no workaround.
        FreeConsole();
    } else {
        // Create job object, which can be attached to child processes
        // to make sure they terminate when the parent terminates as well.
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        job_handle = CreateJobObject(NULL, NULL);

        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
        SetInformationJobObject(job_handle, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    // Start sub-process
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};

    if (CreateProcessW(path, params, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        if (job_handle) {
            AssignProcessToJobObject(job_handle, pi.hProcess);
        }
        ResumeThread(pi.hThread);
    } else {
        if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
            // We must elevate the process, which is (basically) impossible with
            // CreateProcess, and therefore we fallback to ShellExecuteEx,
            // which CAN create elevated processes, at the cost of opening a new separate
            // window.
            // Theoretically, this could be fixed (or rather, worked around) using pipes
            // and IPC, but... this is a question for another day.
            SHELLEXECUTEINFOW sei = {0};

            sei.cbSize = sizeof(SHELLEXECUTEINFOW);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpFile = path;
            sei.lpParameters = params;
            sei.nShow = SW_SHOW;

            if (!ShellExecuteExW(&sei)) {
                fprintf(stderr, "Unable to create elevated process: error %li.", GetLastError());

                exit_code = 1;
                goto cleanup;
            }

            pi.hProcess = sei.hProcess;
        } else {
            fprintf(stderr, "Shim: Could not create process with command '%ls'.\n", path);

            exit_code = 1;
            goto cleanup;
        }
    }

    // Wait till end of process if a job was created
    if (job_handle) {
        // Ignore Ctrl-C and other signals
        if (!SetConsoleCtrlHandler(ctrlhandler, TRUE))
            fprintf(stderr, "Could not set control handler; Ctrl-C behavior may be invalid.\n");

        assert(pi.hProcess);

        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);

        // Dispose of everything
        CloseHandle(pi.hProcess);
        CloseHandle(job_handle);
    }

    CloseHandle(pi.hThread);

cleanup:

  // Free obsolete buffers
    free(path);
    free(args);
    free(params);

    return (int)exit_code;
}
