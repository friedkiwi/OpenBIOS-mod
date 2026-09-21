\
\ Fcode payload for QEMU VGA graphics card
\
\ This is the Forth source for an Fcode payload to initialise
\ the QEMU VGA graphics card.
\
\ (C) Copyright 2013 Mark Cave-Ayland
\

fcode-version3

\
\ Dictionary lookups for words that don't have an FCode
\

: (find-xt)   \ ( str len -- xt | -1 )
  $find if
    exit
  else
    -1
  then
;

" openbios-video-width" (find-xt) cell+ value openbios-video-width-xt
" openbios-video-height" (find-xt) cell+ value openbios-video-height-xt
" depth-bits" (find-xt) cell+ value depth-bits-xt
" line-bytes" (find-xt) cell+ value line-bytes-xt

: openbios-video-width openbios-video-width-xt @ ;
: openbios-video-height openbios-video-height-xt @ ;
: depth-bits depth-bits-xt @ ;
: line-bytes line-bytes-xt @ ;

" fb8-fillrect" (find-xt) value fb8-fillrect-xt
: fb8-fillrect fb8-fillrect-xt execute ;

" fw-cfg-read-file" (find-xt) value fw-cfg-read-file-xt
: fw-cfg-read-file fw-cfg-read-file-xt execute ;

\
\ IO port words
\

" ioc!" (find-xt) value ioc!-xt
" iow!" (find-xt) value iow!-xt
" ioc@" (find-xt) value ioc@-xt

: ioc! ioc!-xt execute ;
: iow! iow!-xt execute ;
: ioc@ ioc@-xt execute ;

" le-w!" (find-xt) value le-w!-xt

: le-w! le-w!-xt execute ;

\
\ PCI
\

" pci-bar>pci-addr" (find-xt) value pci-bar>pci-addr-xt
: pci-bar>pci-addr pci-bar>pci-addr-xt execute ;

h# 10 constant cfg-bar0    \ Framebuffer BAR
h# 18 constant cfg-bar2    \ QEMU MMIO ioport BAR
-1 value fb-addr
-1 value mmio-addr

\
\ VGA registers
\

h# 3c0 constant vga-addr
h# 3c8 constant dac-write-addr
h# 3c9 constant dac-data-addr

defer vga-ioc!

: vga-legacy-ioc!  ( val addr )
  ioc! 
;

: vga-mmio-ioc!  ( val addr )
  h# 3c0 - h# 400 + mmio-addr + c!
;

: vga-color!  ( r g b index -- )
  \ Set the VGA colour registers
  dac-write-addr vga-ioc! rot
  2 >> dac-data-addr vga-ioc! swap
  2 >> dac-data-addr vga-ioc!
  2 >> dac-data-addr vga-ioc!
;

\
\ VBE registers
\

h# 0 constant VBE_DISPI_INDEX_ID
h# 1 constant VBE_DISPI_INDEX_XRES
h# 2 constant VBE_DISPI_INDEX_YRES
h# 3 constant VBE_DISPI_INDEX_BPP
h# 4 constant VBE_DISPI_INDEX_ENABLE
h# 5 constant VBE_DISPI_INDEX_BANK
h# 6 constant VBE_DISPI_INDEX_VIRT_WIDTH
h# 7 constant VBE_DISPI_INDEX_VIRT_HEIGHT
h# 8 constant VBE_DISPI_INDEX_X_OFFSET
h# 9 constant VBE_DISPI_INDEX_Y_OFFSET
h# a constant VBE_DISPI_INDEX_NB

h# 0 constant VBE_DISPI_DISABLED
h# 1 constant VBE_DISPI_ENABLED

\
\ Bochs VBE register writes
\

defer vbe-iow!

: vbe-legacy-iow!  ( val addr -- )
  h# 1ce iow!
  h# 1d0 iow!
;

: vbe-mmio-iow!  ( val addr -- )
  1 lshift h# 500 + mmio-addr + cr .s cr le-w!
;

\
\ Initialise Bochs VBE mode
\

: vbe-init  ( -- )
  h# 0 vga-addr vga-ioc!    \ Enable blanking
  VBE_DISPI_DISABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  h# 0 VBE_DISPI_INDEX_X_OFFSET vbe-iow!
  h# 0 VBE_DISPI_INDEX_Y_OFFSET vbe-iow!
  openbios-video-width VBE_DISPI_INDEX_XRES vbe-iow!
  openbios-video-height VBE_DISPI_INDEX_YRES vbe-iow!
  depth-bits VBE_DISPI_INDEX_BPP vbe-iow!
  VBE_DISPI_ENABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  h# 0 vga-addr vga-ioc!
  h# 20 vga-addr vga-ioc!   \ Disable blanking
;

\
\ Legacy VGA mode setting
\
\ Used for VGA-compatible cards that do not have the QEMU MMIO ioport
\ BAR and Bochs VBE extensions (e.g. the S3 Trio on the PReP 40p
\ machine).  Program a plain 8bpp packed-pixel graphics mode of the
\ requested size through the standard VGA registers; the framebuffer
\ is then accessed linearly through BAR0.
\

: vga-legacy-seq!  ( val idx -- )
  h# 3c4 ioc!  h# 3c5 ioc!
;

: vga-legacy-crtc!  ( val idx -- )
  h# 3d4 ioc!  h# 3d5 ioc!
;

: vga-legacy-gfx!  ( val idx -- )
  h# 3ce ioc!  h# 3cf ioc!
;

: vga-legacy-attr!  ( val idx -- )
  h# 3da ioc@ drop          \ reset attribute flip-flop
  h# 3c0 ioc!  h# 3c0 ioc!
;

: vga-legacy-init  ( -- )
  \ Only 8bpp is supported on this path
  8 depth-bits-xt !
  openbios-video-width line-bytes-xt !

  h# 0 vga-addr ioc!                     \ blank screen
  h# e3 h# 3c2 ioc!                      \ misc output: colour, RAM enable

  h# 01 h# 00 vga-legacy-seq!            \ synchronous reset
  h# 01 h# 01 vga-legacy-seq!            \ 8 dots/char, no clock divide
  h# 0f h# 02 vga-legacy-seq!            \ enable all planes
  h# 00 h# 03 vga-legacy-seq!
  h# 0e h# 04 vga-legacy-seq!            \ ext mem, no odd/even, chain 4
  h# 03 h# 00 vga-legacy-seq!            \ end reset

  h# 00 h# 11 vga-legacy-crtc!           \ unlock CRTC 0-7
  openbios-video-width 3 rshift 1-       \ ( hdisp-1 )
  dup 4 + h# 00 vga-legacy-crtc!         \ horizontal total
  dup h# 01 vga-legacy-crtc!             \ horizontal display end
  dup h# 02 vga-legacy-crtc!             \ horizontal blank start
  drop
  h# 00 h# 03 vga-legacy-crtc!
  h# 00 h# 04 vga-legacy-crtc!
  h# 00 h# 05 vga-legacy-crtc!
  openbios-video-height 1-               \ ( vdisp-1 )
  dup h# ff and h# 06 vga-legacy-crtc!   \ vertical total (low)
  dup h# ff and h# 12 vga-legacy-crtc!   \ vertical display end (low)
  dup h# ff and h# 15 vga-legacy-crtc!   \ vertical blank start (low)
  dup 8 rshift 1 and 1 lshift            \ overflow: vde bit 8
  over 9 rshift 1 and 6 lshift or        \           vde bit 9
  h# 10 or                               \           line compare bit 8
  over 8 rshift 1 and or                 \           vtotal bit 8
  over 9 rshift 1 and 5 lshift or        \           vtotal bit 9
  h# 07 vga-legacy-crtc!
  drop
  h# 00 h# 08 vga-legacy-crtc!           \ preset row scan
  h# 40 h# 09 vga-legacy-crtc!           \ max scan line 0, lc bit 9
  h# 00 h# 0c vga-legacy-crtc!           \ start address
  h# 00 h# 0d vga-legacy-crtc!
  openbios-video-width 3 rshift
  h# 13 vga-legacy-crtc!                 \ offset: line pitch / 8
  h# 40 h# 14 vga-legacy-crtc!           \ doubleword mode
  h# e3 h# 17 vga-legacy-crtc!           \ byte mode, no CGA addressing
  h# ff h# 18 vga-legacy-crtc!           \ line compare (low)

  h# 00 h# 00 vga-legacy-gfx!
  h# 00 h# 01 vga-legacy-gfx!
  h# 00 h# 02 vga-legacy-gfx!
  h# 00 h# 03 vga-legacy-gfx!
  h# 00 h# 04 vga-legacy-gfx!
  h# 40 h# 05 vga-legacy-gfx!            \ 256 colour shift mode
  h# 05 h# 06 vga-legacy-gfx!            \ graphics mode, A0000 64K
  h# 0f h# 07 vga-legacy-gfx!
  h# ff h# 08 vga-legacy-gfx!

  h# 10 0 do
    i i vga-legacy-attr!                 \ identity palette
  loop
  h# 41 h# 10 vga-legacy-attr!           \ graphics, 8-bit colour
  h# 00 h# 11 vga-legacy-attr!
  h# 0f h# 12 vga-legacy-attr!
  h# 00 h# 13 vga-legacy-attr!           \ no pel panning
  h# 00 h# 14 vga-legacy-attr!

  h# ff h# 3c6 ioc!                      \ DAC pel mask

  h# 3da ioc@ drop
  h# 20 vga-addr ioc!                    \ enable video
;

\
\ PCI BAR mapping
\

: map-fb ( -- )
  cfg-bar0 pci-bar>pci-addr if   \ ( pci-addr.lo pci-addr.mid pci-addr.hi size )
    " pci-map-in" $call-parent
    to fb-addr
  then
;

: map-mmio ( -- )
  cfg-bar2 pci-bar>pci-addr if   \ ( pci-addr.lo pci-addr.mid pci-addr.hi size )
    " pci-map-in" $call-parent
    to mmio-addr
  then
;

\
\ Legacy IO port or QEMU MMIO accesses
\
\ legacy: use standard VGA ioport registers
\ MMIO: use QEMU PCI MMIO VGA registers
\
\ If building for QEMU, default to MMIO access since it allows
\ programming of the VGA card regardless of its position in the
\ PCI topology
\

[IFDEF] CONFIG_QEMU
['] vga-mmio-ioc! to vga-ioc!
['] vbe-mmio-iow! to vbe-iow!
[ELSE]
['] vga-legacy-ioc! to vga-ioc!
['] vbe-legacy-iow! to vbe-iow!
[THEN]

\
\ Publically visible words
\

external

[IFDEF] CONFIG_MOL
defer mol-color!

\ Hook for MOL (see packages/molvideo.c)
\
\ Perhaps for neatness this there should be a separate molvga.fs
\ but let's leave it here for now.

: color!  ( r g b index -- )
  mol-color!
;

[ELSE]

\ Standard VGA

: color!  ( r g b index -- )
  vga-color!
;

[THEN]

: fill-rectangle  ( color_ind x y width height -- )
  fb8-fillrect
;

: dimensions  ( -- width height )
  openbios-video-width
  openbios-video-height
;

: set-colors  ( table start count -- )
  0 do
    over dup        \ ( table start table table )
    c@ swap 1+      \ ( table start r table-g )
    dup c@ swap 1+  \ ( table start r g table-b )
    c@ 3 pick       \ ( table start r g b index )
    color!          \ ( table start )
    1+
    swap 3 + swap   \ ( table+3 start+1 )
  loop
;

\
\ Cancel Bochs VBE mode
\

: vbe-deinit ( -- )
  \ Nothing to do for cards without the Bochs VBE extensions
  mmio-addr -1 = if exit then
  \ Switching VBE on and off clears the framebuffer
  VBE_DISPI_DISABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  VBE_DISPI_ENABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  VBE_DISPI_DISABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
;

headerless

\
\ Installation
\

: qemu-vga-driver-install ( -- )
  mmio-addr -1 = if
    map-mmio
    mmio-addr -1 = if
      \ No QEMU MMIO BAR: plain VGA-compatible card, use the legacy
      \ ioports and program the mode through the standard registers
      ['] vga-legacy-ioc! to vga-ioc!
      vga-legacy-init
    else
      vbe-init
    then
  then
  fb-addr -1 = if
    map-fb fb-addr to frame-buffer-adr
    default-font set-font

    frame-buffer-adr encode-int " address" property

    openbios-video-width openbios-video-height over char-width / over char-height /
    fb8-install
  then
;

: qemu-vga-driver-init
  openbios-video-width encode-int " width" property
  openbios-video-height encode-int " height" property
  depth-bits encode-int " depth" property
  line-bytes encode-int " linebytes" property

  \ Is the VGA NDRV driver enabled? (PPC only)
  " /options" find-package drop s" vga-ndrv?" rot get-package-property not if
    decode-string 2swap 2drop    \ ( addr len )
    s" true" drop -rot comp 0= if
      \ Embed NDRV driver via fw-cfg if it exists
      " ndrv/qemu_vga.ndrv" fw-cfg-read-file if
        encode-string " driver,AAPL,MacOS,PowerPC" property
      then
    then
  then

  ['] qemu-vga-driver-install is-install
;

qemu-vga-driver-init

end0
