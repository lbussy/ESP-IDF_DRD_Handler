# Double Reset Detector — ESP-IDF Component

`drd_handler` detects a **double reset** (two resets within a short time
window) and exposes the result through a small C++ API.

Typical use case: let an end user **double-tap reset** to enter a special
mode such as Wi-Fi provisioning, safe mode, or a configuration portal.

This component supports two persistence backends:

- RTC slow memory
- NVS (flash-backed key/value storage)

The NVS backend is recommended for development boards whose reset button
behaves like a power interruption, because RTC slow memory does not survive
power loss.

## Why an NVS backend exists

On many ESP32 development boards, pressing the reset button behaves like a
cold boot:

- RTC slow memory is cleared.
- A DRD implementation that relies only on RTC state cannot observe the
  previous boot marker.

The NVS backend stores DRD state in flash, allowing detection across all
reset types except an explicit NVS erase.

## Firmware identity, flashing, and tooling resets

Flashing tools and debuggers frequently reset a device multiple times during
a single flash or debug session. Without safeguards, those resets may appear
identical to a user-initiated double reset.

To avoid false triggers, the NVS backend incorporates the following
mechanisms:

- **Firmware identity**  
  The running application is identified using the embedded ELF SHA-256
  (`esp_app_desc_t::app_elf_sha256`).
- **Dirty firmware detection**  
  Newly observed firmware images are marked as dirty. DRD is disabled while
  firmware is dirty.
- **Arming delay**  
  After a configurable delay, firmware is marked clean and DRD is armed.
- **Tooling reset suppression (optional)**  
  Reset reasons associated with flashing or debugging may be ignored.

The design intentionally favors *missing* a double reset rather than
false-triggering during flashing or development.

## Kconfig options

The component is configured via Kconfig and exposed through
`idf.py menuconfig`.

### Backend selection

Backend selection is controlled by a Kconfig `choice`:

- `CONFIG_DRD_BACKEND_RTC`
- `CONFIG_DRD_BACKEND_NVS` (default)

Select the NVS backend if the reset button clears RTC slow memory.

### `CONFIG_DRD_SUPPRESS_TOOLING_RESETS`

- Type: `bool`
- Default: `y`

When enabled, the following reset reasons are treated as tooling-related and
do not count as user resets:

- Software reset (`ESP_RST_SW`)
- USB reset (`ESP_RST_USB`)
- JTAG reset (`ESP_RST_JTAG`)

This setting reduces false DRD triggers during flashing and debugging.

### `CONFIG_DRD_WINDOW_SECONDS`

- Type: `int`
- Default: `8`
- Range: `1` to `600`

Time window (in seconds) in which the second reset must occur to count as a
double reset.

### `CONFIG_DRD_NVS_NAMESPACE`

- Type: `string`
- Default: `drd`
- Depends on: `CONFIG_DRD_BACKEND_NVS`

NVS namespace used to store DRD state.

### `CONFIG_DRD_ARM_DELAY_SECONDS`

- Type: `int`
- Default: `10`
- Range: `0` to `600`

Delay after boot before DRD is armed when using the NVS backend and firmware
is considered dirty or tooling resets are detected.

- A value of `0` arms DRD immediately.
- A value greater than `0` arms DRD only after the delay elapses.

## Runtime behavior

### RTC backend

When `CONFIG_DRD_BACKEND_RTC` is selected:

- On first boot, a marker is written to RTC slow memory and a timer is started
  to clear it after `CONFIG_DRD_WINDOW_SECONDS`.
- If a second reset occurs before the timer expires, a double reset is
  detected and the marker is cleared.

RTC state does not survive power loss and may not survive all reset button
implementations.

### NVS backend

When `CONFIG_DRD_BACKEND_NVS` is selected:

- The current application ELF SHA-256 is compared against the stored value in
  NVS.
- If the value is missing or differs:
  - Any prior DRD state is cleared.
  - Firmware is marked dirty.
  - The new SHA-256 value is stored.
- While firmware is dirty, DRD is not armed.
- After `CONFIG_DRD_ARM_DELAY_SECONDS` elapses, firmware is marked clean and
  DRD is armed.
- A second reset within `CONFIG_DRD_WINDOW_SECONDS` then triggers DRD.

If enabled, tooling reset suppression prevents resets generated during
flashing from counting toward DRD detection.

## Building the bundled example

### Managed component usage (default)

When the example is built as part of an ESP-IDF project that uses managed
components, `idf.py` resolves `drd_handler` through the ESP-IDF component
registry based on the dependency declaration in:

```
examples/basic/main/idf_component.yml
```

No additional steps are required in this mode.

### Local component development

When developing `drd_handler` locally (for example, modifying the component
source in this repository), the example can be built against the local
checkout instead of the managed component.

#### Step 1: Enable local development mode

Before configuring the example, set:

```sh
export DRD_HANDLER_LOCAL_DEV=1
```

This signals that the example should use a local component source rather
than a registry-resolved dependency.

#### Step 2: Disable managed dependency resolution

Edit the example component manifest:

```
examples/basic/main/idf_component.yml
```

Comment out the `drd_handler` dependency entry, for example:

```yaml
# dependencies:
#   drd_handler:
#     version: "*"
```

This prevents `idf.py` from attempting to download the component from the
registry.

#### Step 3: Configure and build

From the example directory:

```sh
cd examples/basic
idf.py reconfigure
idf.py build flash monitor
```

In local development mode, the example project makes the component visible
using `EXTRA_COMPONENT_DIRS` and the local repository layout, following
standard ESP-IDF component development practices.

## Integration into an ESP-IDF project

- Place the component under `components/drd_handler/`, or add it as a managed
  dependency.
- Expose the component Kconfig options in your project configuration. One
  common approach is using `Kconfig.projbuild`:

```text
menu "Application configuration"
rsource "components/drd_handler/Kconfig"
endmenu
```

- Ensure the partition table includes an NVS partition when using the NVS
  backend (most ESP-IDF templates already include one).
- Configure DRD options via `idf.py menuconfig`.

### Example usage

```cpp
#include "drd_handler.hpp"

extern "C" void app_main(void)
{
    if (drd_handler::get().check_and_clear())
    {
        // Double reset detected.
        // Enter provisioning, configuration, or safe mode.
    }

    // Normal application startup.
}
```

An explicit detection window may also be supplied:

```cpp
if (drd_handler::get().check_and_clear(8))
{
    // Double reset detected within an 8-second window.
}
```

## Notes and limitations

- The NVS backend performs small, infrequent NVS writes during arming and
  disarming. This is acceptable for normal use, but DRD should not be toggled
  repeatedly in a tight loop.
- If NVS is erased, the next boot is treated as a new firmware image and
  firmware remains dirty until the arm delay elapses.
- DRD state is owned by the singleton returned by `drd_handler::get()`.
  Creating additional instances is unsupported and may result in undefined
  behavior.

---

## License

This component is licensed under the MIT License.
