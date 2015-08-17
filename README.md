# Candela--automatic brightness adjustment daemon
Candela is an auto-brightness daemon designed to adjust screen brightness
based on ambient light sensor input. It is based loosely on
[poliva/lightum][lightum], but has been written from scratch to shed some
dead dbus code (as well as to scratch my own itch).

It is written in C++11 and uses XCB for interacting with the X server (to
get and set the backlight value).

# Configuration
Right now, configuration must be done at compile time by modifying the
source. See the `configuration` struct in the `candela` namespace for
configurable options (they are all commented). By default, the
configuration is set up to work on my own MacBook Pro Retina running Arch
Linux---if you are running some other configuration you'll likely have to
hack things a bit.

In the configuration struct there are two functions that you may want to
customize:
- `ambient_reading()`, which converts the string read from the light
    sensor's output file into a percentage value. By default, this function
    is sufficient for my MacBook, but you may need to change it for e.g. a
    different ambient light sensor.
- `desired_brightness()`, which takes in the maximum supported system
    brightness and the ambient light reading and returns the desired new
    brightness setting. By default, this is set to be the maximum allowed
    brightness scaled by a normalized logarithm of the ambient light source
    reading.

# Building
The build system currently used is CMake, but the project is simple enough
that you should be able to just build it directly by invoking the compiler,
or writing a simple Makefile.

The `CMakeLists.txt` detects the most recent standard available provided by
the compiler and uses that. It will link against libc++ and libc++abi if
able to when `clang++` is used as the compiler.

To build:

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

[lightum]: https://github.com/poliva/lightum
