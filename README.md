# vip_vampirism (Plugify)

[VIP] Vampirism: a VIP whose 'Vampirism' feature value is set heals for that percentage of the damage dealt on player_hurt (clamped to the max health). Ported from the Pisex VIP core module VIP_Vampirism (vip_vampirism). Requires the vip core.

Plugify port (fork) of **VIP_Vampirism ([VIP] Vampirism)** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, vip, hudmenu (optional)

## Installation
Download `vip_vampirism-1.0.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_vampirism`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

