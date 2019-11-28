# K-lapse : A kernel level livedisplay module

### What is it?
Kernel-based Lapse ("K-lapse") is a linear RGB scaling module that 'shifts' RGB based on time or brightness.
This concept is inspired from LineageOS (formerly known as 'CyanogenMod') ROM's feature "livedisplay" which also changes the display settings (RGB, hue, temperature, etc) based on time.

### Why did you decide to make this? (Tell me a story).
I (personally) am a big fan of the livedisplay feature found on LineageOS ROM. I used it every single day, since Android Lollipop.
Starting from Android Nougat, a native night mode solution was added to AOSP and it felt like livedisplay was still way superior,
thanks to its various options (you could say it spoiled me, sure). I also maintained a kernel (Venom kernel) for the device I was using at that time.
It was all good until the OEM dropped support for the device at Android M, and XDA being XDA, was already working on N ROMs.
The issue was, these ROMs weren't LineageOS or based on it, so livedisplay was... gone. I decided I'll try to bring that feature to every other ROM.
How would I do that? Of course! The kernel! It worked on every single
ROM, it was the key! I started to work on it ASAP and here it is, up on
GitHub, licensed under GPLv2 (check klapse.c), open to everyone :)

### How does it work?
Think of it like a f.lux alternative, but inside a kernel and ROM-independent.
Klapse is dependent on an RGB interface (like Gamma on MTK and KCAL on SD chipsets).
It fetches time from the kernel, converts it to local time, and selects and RGB set based on the time. The result is really smooth shifting of RGB over time.
LiveDisplay at the ROM-level, is said to consume some serious battery, but k-lapse doesn't have any extra costs.

### How does it really work (dev)?
Klapse mode 1 (time-based scaling) uses a method `void klapse_pulse(unsigned long data)` that should ideally be called every minute.
This is done using a kernel timer, that is asynchronous so it should be handled with care, which I did (handling races).
The pulse function fetches the current time and makes calculations based on the current hour and the values of the tunables listed down below.

Klapse mode 2 (brightness-based scaling) uses a method `void set_rgb_slider(<type> bl_lvl)` where type is the data type of the brightness level used in your kernel source.
(OnePlus 6 uses u32 data type for bl_lvl)
set_rgb_slider needs to be called/injected inside a function that sets brightness for your device.
(OnePlus 6 uses dsi_panel.c for that, check out the diff for that file in op6 branch)
Since v5.0, mode 2 introduces something called "K-lapse flow", that will try to shift the RGB linearly instead of a sudden change.
(Note that flow depends hugely on how fast KCAL reacts to changes and how frequently can it change without skipping 'rgb frames')

### What all stuff can it do?
1. Emulate smooth transitioning night mode with the proper RGB settings
2. Smoothly scale from one set of RGB to another set of RGB in integral intervals over time.
3. Reduce perceived brightness using dimmer by reducing the amount of color on screen. Allows lower apparent brightness than system permits.
4. Scale RGB based on brightness of display (low brightness usually implies a dark environment, where yellowness is probably useful).
5. Automate the perceived brightness independent of whether klapse is enabled, using its own set of start and stop hours.
6. Be more efficient,faster by residing inside the kernel instead of having to use the HWC HAL like android's night mode.
7. (On older devices) Reduce stuttering or frame lags caused by native night mode.
8. An easier solution against overlay-based apps that run as service in userspace/Android and sometimes block apps asking for permissions.
9. Give you a Livedisplay alternative if it doesn't work in your ROM/your ROM doesn't include it/uses too much battery.
10. Impress your crush so you can get a date (Hey, don't forget to credit me if it works).

### Alright, so this is a replacement for night mode?
Perhaps. One can say this is merely an alternative for LineageOS' Livedisplay, but inside a kernel. Night mode is a sub-function of both Livedisplay and K-lapse.
Most comparisons here were made with night mode because that's what an average user uses, and will relate to the most.
There is absolutely no reason for your Android kernel to not have K-lapse. Go ahead and add it or ask your kernel maintainer to. It's super-easy!

### What can it NOT do (yet)?
1. Grayscale temperature shifts
2. Invert temperature shifts

### I want more! Tell me what can I customize!
All these following tunables are found in their respective files in /sys/module/klapse/parameters/
```python
1. enabled_mode : A switch to enable or disable klapse. Values : 0 = off, 1 = time mode, 2 = brightness mode
2. start_minute : The minute stamp at which klapse should start scaling the RGB values from daytime to target (see next points). Values : 0-1439
3. stop_minute : The minute stamp by which klapse should scale back the RGB values from target to daytime (see next points). Values : 0-1439
4. daytime_r,g,b : The RGB set that must be used for all the time outside of start and stop minutes range (mode 1), or above bl_range_upper (mode 2)
5. target_r,g,b : The RGB set that must be scaled towards for all the time inside of start and stop minutes range (mode 1), or below bl_range_lower (mode 2)
6. target_minutes : Controls how soon the RGB reaches from daytime to target inside of start and stop hour range. Once target is reached, it remains constant till fadeback_minutes (#13) before stop hour, where target RGB scales back to daytime RGB.
7. dimmer_factor : From the name itself, this value has the ability to bend perception and make your display appear as if it is at a lesser brightness level than it actually is at. It works by reducing the RGB values by the same factor. Values : 10-100, (100 means accurate brightness, 50 means 50% of current intensity, you get it)
8. dimmer_factor_auto : A switch that allows you to automatically set the brightness factor in a set time range. Value : 0 = off, 1 = on
9. dimmer_auto_start_minute : The minute stamp at which dimmer_factor should be applied. Works only if #8 is 1. Values : 0-1439
10. dimmer_auto_stop_minute : The minute stamp at which brightness_factor should be reverted to 100. Works only if #8 is 1. Values : 0-1439
11. bl_range_upper and _lower : The brightness range within which klapse should scale from daytime to target_rgb. Works only if #1 is 2. Values : MIN_BRIGHTNESS-MAX_BRIGHTNESS
12. pulse_freq : The amount of milliseconds after which klapse_pulse is called. A more developer-targeted tunable. Only works when one or both of #1 and #8 are 1. Values : 50-600000 (Represents 50ms to 10 minutes)
13. fadeback_minutes : The number of minutes before klapse_stop_hour when RGB should start going back to daytime_rgb. Only works when #1 is 1. Values : 0-minutes between #2 and #3
14. flow_freq :  The amount of milliseconds after which 1 RGB frame is shifted. Somewhat developer-targeted parameter. Only works when mode 2 is enabled. Values : 50-600000 (Represents 50ms to 10 minutes)
```

### Worth noting for kernel manager apps:
1. Dimmer options can all work independent of enabled_mode values. K-lapse doesn't have to be on for dimmer to work. DO NOT HIDE DIMMER OPTIONS IN UI. HAVE A SEPARATE SECTION IN THE PAGE FOR DIMMER CONTROLS.
2. bl_range_upper and bl_range_lower share the same input handler, so if you input a value larger than upper inside the lower file, the value will automatically be assigned to upper. Same goes the other way. Putting a smaller value in upper than lower will change lower instead. It is guaranteed that upper will be a higher value than lower, unless some retard messed it up at compile time.
3. flow_freq is recommended to be added as a slightly not-so-obvious control, because it can change behaviour of scaling and the user will probably blame you or me for being a retard.
4. Parameters only available for use in mode 1 (time) : start_minute, stop_minute, target_minutes, fadeback_minutes.
5. Parameters only available for use in mode 2 (brightness) : bl_range_upper, bl_range_lower, flow_freq
6. Parameters ALWAYS available : daytime_r, daytime_g, daytime_b, target_r, target_g, target_b, pulse_freq, and all the dimmer options.
