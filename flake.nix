{
  description = "A low-power, physically lit gas giant moon observatory for Hyprland";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
  outputs = { self, nixpkgs }: let
    system = "x86_64-linux";
    pkgs = import nixpkgs { inherit system; };
    app = pkgs.stdenv.mkDerivation {
      pname = "gas-giant-screensaver";
      version = "0.1.0";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [ ./CMakeLists.txt ./LICENSE ./src ./shaders ./tests ./assets/giant.png ./assets/mountains.png ];
      };
      nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
      buildInputs = [ pkgs.glfw pkgs.libGL pkgs.libpng ];
      cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
      meta.license = pkgs.lib.licenses.mit;
      doCheck = true;
      checkPhase = "ctest --output-on-failure";
    };
    preview = pkgs.writeShellApplication {
      name = "gas-giant-preview";
      runtimeInputs = [ app pkgs.hyprland pkgs.jq ];
      text = builtins.readFile ./scripts/preview.sh;
    };
    lockFonts = pkgs.makeFontsConf { fontDirectories = [ pkgs.dejavu_fonts pkgs.orbitron ]; };
    lock = pkgs.hyprlock.overrideAttrs (old: {
      pname = "hyprlock-observatory";
      nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.makeWrapper ];
      buildInputs = (old.buildInputs or []) ++ [ pkgs.libpng ];
      patches = (old.patches or []) ++ [
        ./hyprlock/patches/integration.patch
      ];
      postPatch = (old.postPatch or "") + ''
        mkdir -p src/observatory
        cp ${./src}/{renderer,system,emissions}.{cpp,hpp} src/observatory/
        cp ${./hyprlock}/Observatory.{cpp,hpp} src/renderer/widgets/
        cat >> CMakeLists.txt <<'CMAKE'
        find_package(PNG REQUIRED)
        target_link_libraries(hyprlock PRIVATE PNG::PNG)
        target_compile_definitions(hyprlock PRIVATE OBSERVATORY_DATA_DIR="${app}/share/gas-giant-screensaver")
        CMAKE
      '';
      postFixup = (old.postFixup or "") + ''
        wrapProgram "$out/bin/hyprlock" --set FONTCONFIG_FILE ${lockFonts}
      '';
    });
    lockReview = pkgs.writeShellScriptBin "gas-giant-lock" ''
      exec ${lock}/bin/hyprlock --config ${./hyprlock/observatory.conf} "$@"
    '';
  in {
    packages.${system} = { default = app; gas-giant-preview = preview; gas-giant-screensaver = app; hyprlock-observatory = lock; gas-giant-lock = lockReview; };
    apps.${system} = {
      default = { type = "app"; program = "${preview}/bin/gas-giant-preview"; };
      gas-giant-lock = { type = "app"; program = "${lockReview}/bin/gas-giant-lock"; };
    };
    checks.${system} = {
      renderer = app;
      preview = preview;
      hyprlock = lock;
      lock-config = pkgs.runCommand "observatory-lock-config-check" {} ''
        ${lockReview}/bin/gas-giant-lock --check-config
        touch "$out"
      '';
    };
    devShells.${system}.default = pkgs.mkShell {
      packages = [ pkgs.cmake pkgs.ninja pkgs.pkg-config pkgs.glfw pkgs.libGL pkgs.libpng pkgs.python3 ];
    };
  };
}
