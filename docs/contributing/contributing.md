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

See [Testing](testing.md)

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
4. rebase your changes on top of the latest `master` of the main repository
5. push that branch on the forked repository
6. follow the link shown by `git push` to create the merge request
(or alternatively, visit your forked repository on gitlab and create it from there)

While creating the merge request, it is important to enable the
`allow commits from members who can merge to the target branch` option
so that maintainers are able to rebase your branch, since WirePlumber uses
a fast-forward merge policy.

For more detailed information, check out
[gitlab's manual on merge requests](https://docs.gitlab.com/ee/user/project/merge_requests/creating_merge_requests.html)
