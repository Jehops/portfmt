# Changelog

All notable changes to portfmt are documented in this file.
The sections should follow the order `Packaging`, `Added`, `Changed`, `Fixed` and `Removed`.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## Unreleased

### Changed

- Format `UNIQUE_PREFIX_FILES` and `UNIQUE_SUFFIX_FILES` similar to `PLIST_FILES`
- Catch up with FreeBSD Ports:
  - Add new vars like `GO_MODULE`, `KDE_INVENT`
  - Update known `USES={gnome,kde,pyqt,qt}` components
  - Recognize `*_FreeBSD_14` variables
  - Recognize `USES=emacs` flavors

### Fixed

- portfmt: -D produces less cluttered unified diffs with reduced context.
  3 lines of context by default but more can be asked for with an
  optional argument to -D.  Use -D0 to get the full context as before.
- portscan: Handle .include with ${.PARSEDIR}

## [g20200924] - 2020-09-24

No changelog for old releases.
