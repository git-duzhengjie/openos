#requires -version 5.1
$ErrorActionPreference = 'Stop'
Set-Location 'E:\openos'
qemu-system-i386 -m 128M `
  -drive file=target/openos.img,format=raw `
  - serial stdio `
  -netdev tap,id=net0,ifname='OpenOS-TAP',script=no,downscript=no `
  -device e1000,netdev=net0,mac=52:54:00:12:34:56 `
  -display gtk
