{
  description = "Keeper UI — universal ui_qml module (C++ QtRO backend + QML view)";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.0";
    # Dependencies the backend consumes via modules() — attr names MUST match
    # metadata.json "dependencies". keeper + logos_beacon are universal (SYNC
    # forward); stash is legacy (ASYNC only, fire-and-forget + poll).
    keeper.url = "git+file:///home/alisher/basecamp/modules/keeper-basecamp?ref=feat/v0.2-universal-keeper";
    stash.url = "git+file:///home/alisher/basecamp/modules/stash-basecamp?ref=main";
    logos_beacon.url = "git+file:///home/alisher/basecamp/modules/beacon-basecamp?ref=main";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
