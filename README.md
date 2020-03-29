![EmuFlight](docs/assets/images/EmuClassic.png)

# This is Unsanctioned, Unofficial and Unsupported.

* You should probably wait for the official release of 0.3.0.
* This is custom branch of back-ported bug-fixes and optimizations from [EmuFlight code-base](https://github.com/emuflight/EmuFlight).
* It might always report 0.2.22 for compatibility and will not be adding additional *features*.
* Back-porting bug-fixes or optimizations will be as needed; However, this branch will soon stabilize and cease modification as efforts toward official EmuFlight 0.3.0 continues.
* Use at your own risk.
* ***Do NOT report issues to Official EmuFlight.***


### Introduction & Notes

* While far from complete, [EmuFlight Wiki](https://github.com/emuflight/EmuFlight/wiki/What-makes-EmuFlight-different%3F) does have more information than historically.
* Please save a `diff all` and `dump all` prior to upgrading.  Helio/Strix/Flux users may upgrade to IMUF 224 if desired.  Please note that IMUF flashing may corrupt existing configurations, so please upgrade firmware ***after*** saving your data and flashing an [IMUF Release](https://github.com/emuflight/imu-f/releases). Please do read the IMUF Release Notes.
* Firmware flashing must be done with ***Full Chip Erase*** enabled for a clean configuration, free of corruption.
* Please use [EmuConfigurator](https://github.com/emuflight/EmuConfigurator/releases/) version [0.2.12-RC2](https://github.com/emuflight/EmuConfigurator/releases/tag/0.2.12rc2) or newer. Please do read the Configurator Release Notes.
* Please upload the new version 2 fonts using the EmuConfigurator 0.2.12-RC2 or newer.  Note that some flight controllers require being powered from battery to successfully save the fonts.

### Defaults and Presets

* ***Defaults are default.  This should be considered a generic starting point.  Although we feel them to be comfortably safe, please know that tuning your quad is still required, including lowpass filters and possibly notches. Do not flash and "rip", or risk R.I.P.  The same is true for our community-provided presets.  Members of @EmuTestBandits have been using higher than historic dTerm values; However, please be diligent in testing motor-heat in incremental test-hovers of 10, 20, and 30 or more second intervals.  EmuFlight/EmuClassic is provided free as-is without warranty of any kind.***

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
