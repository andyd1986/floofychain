# Floofy Snap Packaging

Commands for building and uploading a Floofy Core Snap to the Snap Store. Anyone on amd64 (x86_64), arm64 (aarch64), or i386 (i686) should be able to build it themselves with these instructions. This would pull the official Floofy binaries from the releases page, verify them, and install them on a user's machine.

## Building Locally
```
sudo apt install snapd
sudo snap install --classic snapcraft
sudo snapcraft
```

### Installing Locally
```
snap install \*.snap --devmode
```

### To Upload to the Snap Store
```
snapcraft login
snapcraft register floofy-core
snapcraft upload \*.snap
sudo snap install floofy-core
```

### Usage
```
floofy-unofficial.cli # for floofy-cli
floofy-unofficial.d # for floofyd
floofy-unofficial.qt # for floofy-qt
floofy-unofficial.test # for test_floofy
floofy-unofficial.tx # for floofy-tx
```

### Uninstalling
```
sudo snap remove floofy-unofficial
```