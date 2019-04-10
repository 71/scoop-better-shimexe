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

[`shim.c`](./shim.c) is:
- **Faster**, because it does not use the .NET Framework, and parses the `.shim` file in a simpler way.
- **More efficient**, because by the time the target of the shim is started, all allocated memory will have been freed.
- And more importantly, it **works better**:
  - Signals originating from pressing `Ctrl+C` are ignored, and therefore handled directly by the spawned child.
    Your processes and REPLs will no longer close when pressing `Ctrl+C`.
  - Children are automatically killed when the shim process is killed. No more orphaned processes and weird behaviors.

## Installation

- In a Visual Studio command prompt, run `cl /O1 shim.c`.
- Replace any `.exe` in `scoop\shims` by `shim.exe`.

An additional script, `repshims.bat`, is provided. It will replace all `.exe`s in the user's Scoop directory
by `shim.exe`.
