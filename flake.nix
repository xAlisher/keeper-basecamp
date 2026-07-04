{
  description = "keeper-basecamp — Internet Archive preservation module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.0";
    nixpkgs.follows = "logos-module-builder/nixpkgs";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
