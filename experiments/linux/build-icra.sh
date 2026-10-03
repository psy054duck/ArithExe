#!/usr/bin/env bash
set -euo pipefail
experiment_dir="${1:?Pass the absolute experiment root}"
cd "$experiment_dir"
export OPAMROOT="$experiment_dir/opam"
export OPAMSWITCH=icra-4.14
eval "$(opam env --root "$OPAMROOT" --switch "$OPAMSWITCH" --set-switch)"
export ICRA_NATIVE_PREFIX=/usr
export LD_LIBRARY_PATH="$experiment_dir/venv/lib/python3.14/site-packages/z3/lib:$OPAMROOT/$OPAMSWITCH/lib/stublibs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
opam install -y --jobs=1 batteries.3.9.0 dune.3.15.3 ocamlgraph.1.8.8 \
  ppx_deriving.5.2.1 ppxlib.0.35.0 oasis.0.4.11 ocamlbuild menhir \
  mlgmpidl.1.3.0 apron.v0.9.15 zarith camlidl
opam pin add -y --no-action conf-ntl "$experiment_dir/icra-dependencies/local-opam/conf-ntl"
opam pin add -y --no-action cil "$experiment_dir/icra-dependencies/cil-source"
opam pin add -y --no-action OCRS "$experiment_dir/icra-dependencies/ocrs-source"
mkdir -p icra-dependencies/ntl-source
tar -xzf archives/9975d8299d4ee149137b1d5b28238bf8 -C icra-dependencies/ntl-source --strip-components=1
opam pin add -y --no-action ntl "$experiment_dir/icra-dependencies/ntl-source"
opam install -y --jobs=1 conf-ntl ntl cil OCRS
mkdir -p icra-dependencies/z3-source
tar -xzf archives/icra-z3-source.tar.gz -C icra-dependencies/z3-source
cd icra-dependencies/z3-source
python3 scripts/mk_make.py --ml --prefix "$OPAMROOT/$OPAMSWITCH"
ln -s "$experiment_dir/venv/lib/python3.14/site-packages/z3/lib/libz3.so" build/libz3.so
make -C build -j1 -o libz3.so api/ml/z3ml.cma api/ml/z3ml.cmxa api/ml/z3ml.cmxs
cd build
ocamlfind install Z3 api/ml/META \
  api/ml/z3enums.mli api/ml/z3enums.cmi api/ml/z3enums.cmx \
  api/ml/z3native.mli api/ml/z3native.cmi api/ml/z3native.cmx \
  ../src/api/ml/z3.mli api/ml/z3.cmi api/ml/z3.cmx \
  api/ml/libz3ml.a api/ml/z3ml.a api/ml/z3ml.cma api/ml/z3ml.cmxa \
  api/ml/z3ml.cmxs api/ml/dllz3ml.so
cd "$experiment_dir/icra"
make -j1 wali
make -j1 duet
make -j1 _build/icra.o _build/icra_callbacks.o _build/ire.o _build/ire_callbacks.o
# Exactly one modern OCaml runtime, shared with the duet library.
g++ -g -rdynamic -o icra _build/icra.o _build/ire_callbacks.o _build/ire.o _build/icra_callbacks.o \
  duet/_build/duet/libduet.native.so WALi-OpenNWA/lib64/libwali.so \
  WALi-OpenNWA/lib64/libwalidomains.so "$(ocamlc -where)/libasmrun_shared.so" \
  -lglog -lrt -ldl -lm \
  -Wl,-rpath,"$experiment_dir/icra/duet/_build/duet" \
  -Wl,-rpath,"$experiment_dir/icra/WALi-OpenNWA/lib64" \
  -Wl,-rpath,"$(ocamlc -where)"
opam list --installed --columns=name,version > "$experiment_dir/logs/icra-opam-inventory.txt"
ldd icra > "$experiment_dir/logs/icra-library-inventory.txt"
./icra --help
