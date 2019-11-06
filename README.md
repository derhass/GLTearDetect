# GLTearDetect

GLTearDetect is a small GL tool to generate some patterns which should help in
identifying screen tearing due to sync to vblank issues. Additionally, the
swap interval can be set directly by the application.

## Patterns

The following patterns are provided.

* None: will render nothing, might be useful to check for FPS measurements
* Color Cycler: cycle through the eight edges of the RGB cube (epilepsy warning)
* Color Pulse: pulse between black and white
* Bars: horizontally moving vertical bars (the default)

The speed for Color Pulse and Bars can be controlled via keyboard.

## Keybord Control

* `Left`/`Right` or `SPACE`/`BACKSPACE`: cycle through available patterns
* `Up`/`Down`: increase/decrease speed (for bars and pulse)
* `S`: toggle Sync-To-VBlank (Interval between 0 and 1)
* `Shift-S`: set the currently selected swap interval
* `W` or `ENTER`: re-create current window, swap interval will not be set
  automatically, but left at system's default
* `F`: toggle (windowed) fullscreen (recreates window)
* `Shift-F`: toggle fullscreen (recreates window)
* `+`/`-`: increase/decrease swap interval (and apply it)
* `*`/`/`: increase/decrease bar width
* `HOME`: reset speeds and sizes
* `PageUp`/`PageDown`: cycle through different Swap Interval modes
* `V`/`Shift-V`: increade/decrease the additional CPU sleep time per frame by 1 ms
* `B`/`Shift-B`: increade/decrease the additional CPU busy wait time per frame by 1 ms
* `C`: toggle forced CPU <-> GPU synchronzation per frame (`glFinish`)
* `Shift-C`: toggle forced GPU queue flush per frame (`glFlush`)

(Note: keyboard mapping assumes US layout always)

## Program Output

The program will regularily print info to the standard output
(text console on windows), and also update the window title

The output will be in the form of:

    GLTearDetect: [swap_interval_mode:interval] FPS Latency Last_Latency [flush] [finish] sleep busywait

`FPS` (frames per second) and `Latency` (time between the `SwapBuffers` call and
the actual buffer swap) are the averages over a period of one second, and
`Last_Latency` will denote the actual latency of one frame at the time the info
was generated (minus 10 frames).

`flush` or `finish` forced GL CPU <-> GPU synchronization and are only shown when enabled (keys `C` and `Shuft-C`),
`sleep` and `busywait` show additional time the CPU was put to sleep or to busy waiting per frame (keys `V`, `B`)
to simulate some CPU load of a graphical application.
    
## Used Libraries

Besides OpenGL itself, the following library is used:
* GLFW3 (http://www.glfw.org/) is used as a multi-platform library for creating
  windows and OpenGL contexts and for event handling.  For 32Bit Windows, the
static libraries built with Visual Studio 2010 to 2015 are included (together
with the header files), so that no extra libraries have to be installed here.
For other platforms, you should install the GLFW library >= 3.0 manually.

Furthermore, glad (https://github.com/Dav1dde/glad) was used to generate the C code for the OpenGL,
WGL and GLX loaders. The loader source code is directly integrated into this project, glad itself is
neither required as a build nor runtime dependency.

## Requirements

For windows, project files for Visual Studio 2010 to 2015 are provided. They generate statically linked
binaries which should run out-of-the-box even when copied to another system. For Linux with GNU make,
a simple Makefile is provided. It tries to findthe GLFW3 library via pkg-config.

To run this program, you need an OpenGL implementation supporting (at least) GL 3.3 
in core profile. (Conceptually, the shaders could be easily backported to support GL
2.0, and the timer queries could be made optional, but I don't really care
about ancient GPUs at this point.)

