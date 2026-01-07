# drd_handler basic example

This example shows how to use the `drd_handler` component to detect a user
double-reset event and switch into an alternate "mode" at boot.

- First boot: the detector arms a window.
- Second reset within the configured window: `check_and_clear()` returns `true`.

## Behavior

- No double reset: LED blinks at `CONFIG_EXAMPLE_BLINK_NORMAL_MS`.
- Double reset detected: LED blinks at `CONFIG_EXAMPLE_BLINK_DRD_MS`.

## Build and flash

From this folder:

```sh
idf.py set-target <your-target>
idf.py build flash monitor
```

## Notes

- The example calls `drd_handler::get().configure()` once at startup.
- It then calls `drd_handler::get().check_and_clear()` which uses the component's
  `CONFIG_DRD_WINDOW_SECONDS` window.
