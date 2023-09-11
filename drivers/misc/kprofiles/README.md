# Kprofiles

## Table of Contents

1. [About](#about)
2. [Supported Kernel Version](#supported-kernel-versions)
3. [Usage](#using-kprofiles-in-your-linux-kernel-source)
4. [Available APIs](#available-apis)
5. [How To Contribute](#how-to-contribute)
6. [License](#license)
7. [Our Team](#team)

## About

Kprofiles is a simple Linux kernel module that can be used to regulate in-kernel activities such as boosts that are normally not exposed to the userspace. Kprofiles operates in a profile-oriented manner So, It has four profile modes: `Disabled`, `Battery`, `Balanced`, and `Performance`. Each mode is allocated a mode number. Developers can use the Kprofiles API function in conditions to limit or enable in-kernel tasks in a certain profile mode **_(look at [usage section](#using-kprofiles-in-your-linux-kernel-source) for example)_**. Userspace can interact with Kprofiles by adjusting the mode number in `/sys/kernel/kprofiles/kp_mode`. The table below shows which mode number corresponds to the profile mode.

| Profile Mode       | Mode Number |
| ------------------ | ----------- |
| Disabled (default) | 0           |
| Battery            | 1           |
| Balanced           | 2           |
| Performance        | 3           |

Furthermore, Kprofiles provides automatic profile changer (auto Kprofiles), which uses `FB notifier`, `MSM DRM notifier` or `MI DRM notifier` to enforce battery profile mode when the device's screen goes off and switches back to previously active mode when the device wakes up. Users can disable or enable this feature at runtime without recompiling the kernel by changing the bool value in `/sys/module/kprofiles/parameters/auto_kp`

Kprofiles additionally has API functions for switching profiles in response to any in-kernel event. For further information, please see the table of [Available APIs](#available-apis)

## Supported Kernel Versions

Kprofiles is tested on Linux 3.10+ kernels

## Using kprofiles in your Linux kernel source

If you're using `git`, using `git subtree` or `git submodule` is highly recommended.

1. Add this repository to `drivers/misc/kprofiles`

2. Modify `drivers/misc/Kconfig`

```diff
menu "Misc devices"

source "drivers/misc/genwqe/Kconfig"
+source "drivers/misc/kprofiles/Kconfig"
source "drivers/misc/echo/Kconfig"
endmenu
```

3. Modify `drivers/misc/Makefile`

```diff
obj-$(CONFIG_GENWQE)     += genwqe/
+obj-$(CONFIG_KPROFILES) += kprofiles/
obj-$(CONFIG_ECHO)       += echo/
```

4. Define the api function you need in the required file using extern, Kprofiles can be used in various places like for example boosting drivers, below is an example of using kprofiles in kernel/fork.c to control cpu and ddr boosts during zygote forking using kp_active_mode() API.

```diff
+ #ifdef CONFIG_KPROFILES
+ extern int kp_active_mode(void);
+ #endif
/*
 *  Ok, this is the main fork-routine.
 *
 * It copies the process, and if successful kick-starts
 * it and waits for it to finish using the VM if required.
 */
long _do_fork(unsigned long clone_flags,
          unsigned long stack_start,
          unsigned long stack_size,
          int __user *parent_tidptr,
          int __user *child_tidptr,
          unsigned long tls)
{
    struct task_struct *p;
    int trace = 0;
    long nr;

    /* Boost CPU to the max for 50 ms when userspace launches an app */
    if (task_is_zygote(current)) {
-       cpu_input_boost_kick_max(50);
-       devfreq_boost_kick_max(DEVFREQ_MSM_LLCCBW, 50);
-       devfreq_boost_kick_max(DEVFREQ_MSM_CPUBW, 50);
+ #ifdef CONFIG_KPROFILES
+   /*
+    * Boost CPU and DDR for 60ms if performance mode is active.
+    * Boost CPU and DDR for 50ms if default mode is active to retain default behaviour.
+    * Boost CPU & DDR for 25ms if balanced profile is enabled
+    * Dont boost CPU & DDR if battery saver profile is enabled
+    */
+       switch (kp_active_mode()) {
+       case 0:
+           cpu_input_boost_kick_max(60);
+           devfreq_boost_kick_max(DEVFREQ_MSM_LLCCBW, 50);
+           devfreq_boost_kick_max(DEVFREQ_MSM_CPUBW, 50);
+           break;
+       case 2:
+           cpu_input_boost_kick_max(60);
+           devfreq_boost_kick_max(DEVFREQ_MSM_LLCCBW, 25);
+           devfreq_boost_kick_max(DEVFREQ_MSM_CPUBW, 25);
+           break;
+       case 3:
+           cpu_input_boost_kick_max(60);
+           devfreq_boost_kick_max(DEVFREQ_MSM_LLCCBW, 60);
+           devfreq_boost_kick_max(DEVFREQ_MSM_CPUBW, 60);
+           break;
+       default:
+           pr_info("Battery Profile Active, Skipping Boost...\n");
+           break;
+       }
+ #else
+       cpu_input_boost_kick_max(60);
+       devfreq_boost_kick_max(DEVFREQ_MSM_LLCCBW, 50);
+       devfreq_boost_kick_max(DEVFREQ_MSM_CPUBW, 50);
+ #endif
```

and you are good to go!

## Available APIs

### kp_active_mode()
- **Info**: This function returns a number from 0 and 3 depending on the profile selected. It can be used in conditions to regulate boosts and other things.
- **Arguments**: None
- **Example**:
```c
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_KPROFILES
extern int kp_active_mode(void);
void example_function(void)
{
    // Based on the active profile mode, tune kernel features or settings
    switch (kp_active_mode()) {
        case 1:
            // Things to be done when battery profile is active
            break;
        case 2:
            // Things to be done when balance profile is active
            break;
        case 3:
            // Things to be done when performance profile is active
            break;
        default:
            // Things to be done when kprofiles is disabled or default profile is active
            break;
    }
}
#endif
```

### kp_set_mode()
- **Info**: This function can be used to change profile to any given mode during any in-kernel event.
- **Arguments**:
  - `level` :
    <br>type: unsigned int
    <br>definition: The mode number to switch to. 
- **Example**:
```c
void example_function(void)
{
    if (required_kernel_event()) {
        // Switch to battery profile (mode 0)
        kp_set_mode(1);
        printk(KERN_INFO "Switched to battery profile (mode 1)\n");
    } else {
        // Switch to balanced profile (mode 1)
        kp_set_mode(2);
        printk(KERN_INFO "Switched to balanced profile (mode 2)\n");
    }
}
```

### kp_set_mode_rollback()  
- **Info**: This function can be used to change profile to any given mode for a specific period of time during any in-kernel event, then return to the previously active mode.
- **Arguments**:
  - `level` :
    <br>*type*: unsigned int
    <br>*definition*: The mode number to switch to.
  - `duration_ms` :
    <br>*type*: unsigned int
    <br>*definition*: The amount of time (in milliseconds) the specified mode should be active before switching back to the previously used mode.
- **Example**:
```c
void example_function(void)
{
    if (required_kernel_event()) {
        // Switch to performance profile (mode 3) for 50ms
        kp_set_mode_rollback(3, 50);
        printk(KERN_INFO "Switched to Performance profile for 50ms\n");
    }
}
```

### Profile mode event notifier  
- **Info**: This basic event notifier enables the execution of a sequence of code as soon as a profile mode is changed through user or in-kernel APIs. This can be advantageous for performing a series of tasks in a synchronized manner following a mode change.
- **Example**:
```c
extern unsigned int KP_MODE_CHANGE;
extern int kp_notifier_register_client(struct notifier_block *nb);
extern int kp_notifier_unregister_client(struct notifier_block *nb);

static int kp_mode_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
    unsigned int profile_mode = (unsigned int)data;

    if (event == KP_MODE_CHANGE) {
        switch (profile_mode) {
            case 1:
                // Things to be done when battery profile is active
                break;
            case 2:
                // Things to be done when balanced profile is active
                break;
            case 3:
                // Things to be done when performance profile is active
                break;
            default:
                // Things to be done when kprofiles is disabled or default profile is active
                break;
        }
    }

    return NOTIFY_OK;
}

static struct notifier_block kp_mode_notifier = {
    .notifier_call = kp_mode_notifier_callback,
};

static int __init example_driver_init(void)
{
    // Register the driver module as a client of the Kprofiles event notifier
    kp_notifier_register_client(&kp_mode_notifier);

    return 0;
}

static void __exit example_driver_exit(void)
{
    // Unregister the driver module as a client of the Kprofiles event notifier
    kp_notifier_unregister_client(&kp_mode_notifier);
}

module_init(example_driver_init);
module_exit(example_driver_exit);
```

## How To Contribute

You can contribute to Kprofiles by sending us pull requests with the changes,
Please provide a precise message with the commit explaining why you're making the change.

## License

This project is licensed under GPL-2.0.

## Team

- [Dakkshesh](https://github.com/dakkshesh07) : kernel module Author/Maintainer
- [Cyberknight777](https://github.com/cyberknight777) : kernel module Co-Maintainer | Kprofiles Manager Maintainer
- [Vyom Desai](https://github.com/CannedShroud) : Kprofiles Manager [(rom package)](https://github.com/CannedShroud/android_packages_apps_KProfiles) Author/Maintainer
- [Martin Valba](https://github.com/Martinvlba) : Kprofiles Manager [(standalone app)](https://github.com/dakkshesh07/Kprofiles/tree/app) Author/Maintainer
- [Someone5678](https://github.com/someone5678) : Kprofiles Manager [(rom package)](https://github.com/CannedShroud/android_packages_apps_KProfiles) Co-Maintainer
