# vip_vampirism (Plugify)

[VIP] Vampirism: a VIP whose 'Vampirism' feature value is set heals for that percentage of the damage dealt on player_hurt (clamped to the max health). Ported from the Pisex VIP core module VIP_Vampirism (vip_vampirism). Requires the vip core.

Plugify port (fork) of **VIP_Vampirism ([VIP] Vampirism)** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugin version: 1.2.0
- Plugify dependencies: s2sdk (>=2.18.2), vip (>=1.2.7)
- Self-contained s2sdk integration: uses s2sdk directly, without the pisexcompat library.

## Round policy
The gameplay modules vip_regen_hp, vip_regen_armor, vip_vampirism, vip_jumps and vip_duckspeed use completed-round thresholds configured per group. The default `1` skips both pistol rounds and requires at least one completed round; `0` explicitly opts out; `N >= 2` sets a completed-round threshold.
Group keys: `RegenHP_round_min`, `RegenArmor_round_min`, `Vampirism_round_min`, `jumps_round_min`, `duckspeed_round_min`.
VIP core `>= 1.2.7` keeps restricted effects blocked after a late core load until the next prestart.

## Installation
Download `vip_vampirism-1.2.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_vampirism`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

