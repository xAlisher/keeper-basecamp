{
  description = "keeper-basecamp — Internet Archive preservation module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nixpkgs.follows = "logos-module-builder/nixpkgs";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      # libQt6HttpServer and libQt6WebSockets are not in the Basecamp AppImage — bundle them.
      # libQt6RemoteObjects IS in the AppImage; do NOT bundle it (double-loading causes heap corruption).
      postInstall = ''
        for mod in Qt6HttpServer Qt6WebSockets; do
          libdir=$(pkg-config --variable=libdir $mod 2>/dev/null || true)
          [ -z "$libdir" ] && continue
          soname="lib''${mod}.so.6"
          [ -f "$libdir/$soname" ] && cp -L "$libdir/$soname" "$out/lib/"
        done
      '';
    };
}
