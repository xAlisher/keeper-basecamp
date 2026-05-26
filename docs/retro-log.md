# Retro Log

## win 2026-05-26
keeper crash fixed — pre-init IPC clients before network activity avoids bad_alloc from QRemoteObjectNode in post-download Qt RO socket state

## win 2026-05-26
keeper full chain confirmed — download→stash→beacon→txHash→UI with Clear+copy buttons, clipboard fix (opacity:0 not visible:false), QML deploy path confirmed (plugins/ not modules/)

## win 2026-05-26
Qt6HttpServer nix build unblocked — qt6.qthttpserver in nix.packages.runtime + QTcpServer::listen+bind + QHttpHeaders API fixes

## win 2026-05-26
Log panel redesigned — Stash → Logos Storage + Beacon → Logos Blockchain two-line format with selectable CID; file status bug (uploaded→done) and startup load bug fixed

## fail 2026-05-26
stash attribution overstated as blocker for keeper — keeper calls beacon directly with source="keeper"; stash fix only relevant for modules relying on stash auto-inscription path
