# Contributing to iHub-Pro Open Firmware

Thanks for helping improve an open grow controller for the community! 🌱

## Ground rules

- Be respectful. This is a hobby project for home growers.
- One logical change per pull request; keep diffs focused.
- Match the existing code style (the firmware is plain C / ESP-IDF; the web UI is
  vanilla HTML/CSS/JS with no build step).
- Test on real hardware when you can, and say what you tested in the PR.

## Building

See the [README](README.md#build--flash). In short:

```
pio run -e ihub                 # build
pio run -e ihub -t upload       # flash over USB
```

## Licensing of contributions (please read)

This project is dual-track: it is published under the **GNU GPL-3.0** for everyone,
and the maintainer also offers a separate **commercial license** to companies that
cannot accept the GPL (see [COMMERCIAL-LICENSE.md](COMMERCIAL-LICENSE.md)). To keep
both tracks possible, contributions are accepted under the following terms.

### 1. Developer Certificate of Origin (DCO)

Every commit must be signed off, certifying you wrote the code or have the right to
submit it (full text: <https://developercertificate.org/>). Add a sign-off line:

```
Signed-off-by: Your Name <your.email@example.com>
```

The easy way is `git commit -s` (and `git rebase --signoff` for existing commits).

### 2. License grant

By submitting a contribution you agree that:

- Your contribution is licensed to everyone under the **GPL-3.0** (inbound = outbound), **and**
- You grant the project maintainer a **perpetual, worldwide, non-exclusive, royalty-free,
  irrevocable license** to use, modify and **relicense** your contribution, including as
  part of a commercial/proprietary license of this project.

You keep the copyright to your contribution. This grant is only what lets the
maintainer keep offering the commercial-license option without having to track down
every contributor. If you are not comfortable with the relicensing grant, open an
issue first — small fixes can usually be handled differently.

> This file is a plain-language summary, not legal advice.

## Good first contributions

- **Internationalisation:** the UI ships in German + English (runtime dictionary in
  `main/i18n_js.h`) — adding more languages, or improving the English strings, is welcome.
- New sensor / device support on the RS-485 / RJ12 bus.
- Documentation fixes in the [Handbook](docs/HANDBOOK.md).
