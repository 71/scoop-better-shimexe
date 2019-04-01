[Scoop](https://github.com/lukesampson/scoop), although very useful and nice to use,
uses a (subjectively) terrible [`shim.exe`](https://github.com/lukesampson/scoop/blob/master/supporting/shimexe/shim.cs)
file to redirect commands from `scoop\shims\app.exe` to `scoop\apps\app\current\app.exe`, because:
1. [It's made in C#](https://github.com/lukesampson/scoop/tree/master/supporting/shimexe),
   and thus requires an instantiation of a .NET command line app everytime it is started,
   which can make a command run much slower than if it had been ran directly;
2. [It](https://github.com/lukesampson/scoop/issues/2339) [does](https://github.com/lukesampson/scoop/issues/1896)
   [not](https://github.com/felixse/FluentTerminal/issues/221) handle Ctrl+C and other
   signals correctly, which can be quite infuriating.

The last issue making interaction with REPLs and long-running apps practically impossible,
and having never been fixed, I set out to improve them with this repository.

[`shim.c`](./shim.c) is a C program which avoids unnecessary allocations and simply wraps a
call to [`CreateProcess`](https://docs.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-createprocessa),
essentially doing the exact same thing as the existing `shim.exe`, without the needless .NET framework.

Additionally, it ignores Ctrl+C and other signals, giving full control of these signals to the child process.

## Installation

- In a Visual Studio command prompt, run `cl /O1 shim.c`.
- Replace any `.exe` in `scoop\shims` by `shim.exe`.

An additional script, `repshims.bat`, is provided. It will replace all `.exe`s in the user's Scoop directory
by `shim.exe`.
