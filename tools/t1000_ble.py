#!/usr/bin/env python3
"""Redirection vers platform-gr551x/tools/t1000_ble.py (sous-module, seule source de l'outil).

Conservé pour les commandes et la documentation existantes, et pour l'import `from t1000_ble import …`.
"""
import importlib.util, os, sys

_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'platform-gr551x', 'tools', 't1000_ble.py')
if not os.path.exists(_PATH):
    sys.exit('platform-gr551x absent : git submodule update --init')
_spec = importlib.util.spec_from_file_location('_platform_t1000_ble', _PATH)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)
globals().update({k: v for k, v in vars(_mod).items() if not k.startswith('__')})

if __name__ == '__main__':
    _mod.main()
