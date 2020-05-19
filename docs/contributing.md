# Contributing to WirePlumber

## Coding style

WirePlumber uses the
[GNOME C Coding Style](https://developer.gnome.org/programming-guidelines/unstable/c-coding-style.html.en)
with K&R brace placement and 2-space indentation, similar to
[GStreamer](https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/developing.html#what-is-the-coding-style-for-gstreamer-code).
When in doubt, just follow the example of the existing code.

WirePlumber ships with a [.editorconfig](https://editorconfig.org/) file.
If your code editor / IDE supports this, it should automatically set up
the indentation settings.

> When submitting changes for review, please ensure that the coding style
of the changes respects the coding style of the project

## Tests

WirePlumber has automated tests that you can easily run with:

```
$ meson test -C build
```

This will automatically compile all test dependencies, so you can be sure
that this always tests your latest changes.

If you wish to run a specific test instead of all of them, you can run:
```
$ meson test -C build test-name
```

When debugging a single test, you can additionally enable verbose test output
by appending `-v` and you can also run the test in gdb by appending `--gdb`.

For more information on how to use `meson test`, please refer to
[meson's manual](https://mesonbuild.com/Unit-tests.html)

> When submitting changes for review, always ensure that all tests pass

## Running in gdb / valgrind / etc...

The Makefile included with WirePlumber supports the `gdb` and `valgrind`
targets. So, instead of `make run` you can do `make gdb` or `make valgrind`
to do some debugging.

You can also run in any other wrapper by setting the `DBG` variable
on `make run`. For example:
```
$ make run DBG="strace"
```

## Merge requests

In order to submit changes to the project, you should create a merge request.
To do this,
1. fork the project on https://gitlab.freedesktop.org/pipewire/wireplumber
2. clone the forked project on your computer
3. make changes in a new git branch
4. push that branch on the forked project
5. follow the link shown by `git push` to create the merge request
(or alternatively, visit your forked project on gitlab and create it from there)

For more detailed information, check out
[gitlab's manual on merge requests](https://docs.gitlab.com/ee/user/project/merge_requests/creating_merge_requests.html)
