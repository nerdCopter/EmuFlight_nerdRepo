![EmuClassic](https://user-images.githubusercontent.com/56646290/80221765-5e1de080-860b-11ea-93e4-1cae280423b9.png)

# This is Unsanctioned, Unofficial and Unsupported.

* This is custom branch of back-ported bug-fixes and optimizations from [EmuFlight code-base](https://github.com/emuflight/EmuFlight) 0.2.X versions.
* It might always report 0.2.22 for compatibility and will not be adding additional *features*.
* Use at your own risk.
* ***Do NOT report issues to Official EmuFlight.***

### Introduction & Notes

* While far from complete, [EmuFlight Wiki](https://github.com/emuflight/EmuFlight/wiki/What-makes-EmuFlight-different%3F) does have more information than historically.
* Please save a `diff all` and `dump all` prior to upgrading.
* Helio/Strix/Flux users may upgrade to IMUF 224 if desired, but such is not necessity.  Please note that IMUF flashing may corrupt existing configurations, so please upgrade firmware ***after*** saving your data and flashing an [IMUF Release](https://github.com/emuflight/imu-f/releases). Please do read the IMUF Release Notes. ***Do not use an IMUF version higher than IMUF 224 with this EmuFlight firmware version.***
* Firmware flashing must be done with ***Full Chip Erase*** enabled for a clean configuration, free of corruption.
* Please use [EmuConfigurator](https://github.com/emuflight/EmuConfigurator/releases/) version [0.2.12-RC2](https://github.com/emuflight/EmuConfigurator/releases/tag/0.2.12rc2) or newer. Please do read the Configurator Release Notes.
* Please upload the new version 2 fonts using the EmuConfigurator 0.2.12-RC2 or newer.  Note that some flight controllers require being powered from battery to successfully save the fonts.

### Defaults and Presets

* ***Defaults are only defaults.  This should be considered a generic starting point for 5inch quads.  Although we feel them to be comfortably safe, please know that tuning your quad is still required, including lowpass filters and possibly notches. Do not flash and "rip", or risk R.I.P.  The same is true for our community-provided presets.  Members of @EmuTestBandits have been using higher than historic dTerm values; However, please be diligent in testing motor-heat in incremental test-hovers of 10, 20, and 30 or more second intervals.  EmuFlight/EmuClassic is provided free as-is without warranty of any kind.***
* These builds have custom defaults.

### Tuning

* Tuning should be done very similarly to Betaflight 3.5.x tuning methods; However, you should also use the [Tuning Flowchart](https://github.com/emuflight/EmuFlight/wiki/IMUF) presented on the Wiki.

#### This version specifically contains:

* Classic Gyro and dTerm LPF's (Gyro LPF1 & LPF2; dTerm LPF1 & LPF2)
* SPA Roll/Pitch Mixed and SPA Yaw
* Working Dynamic Notch (Gyro)
* [Angle/NFE-Racer](https://github.com/emuflight/EmuFlight/wiki/Angle-and-NFE-Racer-modes) mode PID controller (from Silverware)
* [Smart dTerm Smoothing](https://github.com/emuflight/EmuFlight/wiki/smart-dTerm-smoothing) (CLI Only)
* [WitchCraft](https://github.com/emuflight/EmuFlight/wiki/WitchCraft) (from RaceFlight) (CLI Only)

#### This version specifically does not contain:

* Gyro and dTerm LPF's per-axis  (Gyro Roll/Pitch/Yaw LPF1 & R/P/Y LPF2; dTerm R/P/Y LPF1 & R/P/Y LPF2)
* Stick-PIDs (Replaces Cinematic SetPoint)
* Cinematic SetPoint (Neither does Official)
* IMUF Sharpness

### *Huge Thank You to:*
 * DevTeam
 * TestBandits
 * Contributors
 * Community
 * & Especially to ThoseWeRideTheCoatTailsOf.

### Why does this exist?
 * Initially, I (nerdCopter) was authorized (with some pleading) to release an interim build to allow end-users to fly more recent firmware, while work toward 0.3.0 continued.  I requested to do this interim release, because a decision was made to separate Lowpass Filters per axis.  While technically this seems a good idea, I felt in practice this is just more work for end-users.  A decision was made by the development community not to release an interim build.  I went ahead and released, on my own GitHub repository, the work I had done up to that point, but after inspecting some code, I realized that some of my merges where botched due to some of my misunderstandings of the `git` version-control system.  This is a total rebuild starting at EmuFlight commit 359e767e7 with many back-ported fixes, additions, and optimizations courtesy of other participants. The flight-code is not my creation; However, it is the aggregation of specific code into an unofficial firmware I'm dubbing as EmuClassic.  I have been flying variations of this branch for about a month.  You will need to re-tune, but this is totally flyable and much-inline with official EmuFlight development versions with the exception of personally chosen [TPA](https://github.com/emuflight/EmuFlight/wiki/TPA), [SPA](https://github.com/emuflight/EmuFlight/wiki/SPA), PID, [SDS](https://github.com/emuflight/EmuFlight/wiki/smart-dTerm-smoothing), and [WC](https://github.com/emuflight/EmuFlight/wiki/WitchCraft) defaults.  Please no kudos to me, instead i rather you focus on support of the official "EmuFlight" rather than "EmuClassic" or "0.2.22".  Thank you.

### Compiling

#### Debian/Ubuntu Prerequisites
```shell
sudo apt install git build-essential
```

#### Fedora/RHEL/CentOS  Prerequisites
```
sudo yum groupinstall "Development Tools" "Development Libraries"
## or
## dnf install @development-tools
```

#### Cloning and Building
```shell
git clone --shallow-since="Mar 26 2020" https://github.com/nerdCopter/EmuClassic_nerdCopter.git
cd EmuClassic_nerdCopter

## as needed upon arm_sdk updates:
make arm_sdk_install --always-make

## building: make [all | target [target] ... [target] [--always-make] [--keep-going]
## e.g. make all --always-make --keep-going
## where --always-make will force build all dependencies & --keep-going will continue upon a target error
make HELIOSPRING FOXEERF405
```
