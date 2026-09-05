#!/usr/bin/env python3
"""Reproduce MediaMuster's MC 26.8 effect catalogue from the installed files.

This extractor is deliberately pinned to the two reviewed ARM64 binaries.
A newer build requires instruction review, not a silent version substitution.
Only output/cache files are written; the installed application is read-only.
"""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {
    'libame': '70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e',
    'plugin': '4e9de133828e4eb36c57f7ead856ae7715d957e79ace45ab4662ddfaab1a51f4',
}
BUILTIN = '__ZN7AEffect27SetEffectInfoFromIdentifierEii'
PLUGIN_INIT = '__GLOBAL__sub_I_MCEffects.cpp'
AUDIO = '__ZN14AAudioDissolve13GetAllEffectsEi'
VISITOR = '__ZN22EffectComponentVisitor18GenerateEffectInfoEP10AComponent'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def arm64(data):
    if data[:4] == b'\xcf\xfa\xed\xfe':
        return data
    assert data[:4] == b'\xca\xfe\xba\xbe', 'Unsupported Mach-O container'
    for i in range(struct.unpack_from('>I', data, 4)[0]):
        cpu, _, offset, size, _ = struct.unpack_from('>IIIII', data, 8 + i * 20)
        if cpu == 0x100000c:
            return data[offset:offset + size]
    raise ValueError('No ARM64 slice')


def disassemble(binary, output):
    with output.open('w') as stream:
        subprocess.run(['xcrun', 'llvm-objdump', '--macho', '--arch=arm64',
                        '--disassemble', str(binary)], stdout=stream, check=True)


def functions(path, wanted):
    result, current = {}, None
    for line in path.open():
        if line and not line[0].isspace():
            symbol = line.strip().removesuffix(':')
            current = symbol if wanted(symbol) else None
        if current:
            result.setdefault(current, []).append(line)
    return {k: ''.join(v) for k, v in result.items()}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--install', type=Path, default=Path('/Applications/Avid Media Composer'))
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--libame-asm', type=Path, help='Optional existing llvm-objdump --macho output')
    parser.add_argument('--plugin-asm', type=Path)
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    app = args.install / 'AvidMediaComposer.app'
    libame = app / 'Contents/MacOS/libameLibrary.dylib'
    plugin = app / 'Contents/SharedSupport/AVX2_Plug-ins/MCEffects.avx/Contents/MacOS/MCEffects'
    manifest = {'version': plistlib.loads((app / 'Contents/Info.plist').read_bytes()).get('CFBundleShortVersionString'),
                'files': [], 'architecture': 'arm64', 'mcFirst': False,
                'alphaFlex': [False, True], 'otherFeatureToggles': [False, True]}
    for key, source in [('libame', libame), ('plugin', plugin)]:
        data = source.read_bytes()
        selected = arm64(data)
        assert sha(selected) == EXPECTED[key], f'{key} changed; review instructions before extracting'
        manifest['files'].append({'path': str(source.relative_to(args.install)),
                                  'sha256': sha(data), 'arm64Sha256': sha(selected)})
        target = args.cache / (key + '-arm64')
        target.write_bytes(selected)
        os.environ['AVID_EFFECT_LIBAME' if key == 'libame' else 'AVID_EFFECT_PLUGIN'] = str(target)
    lib_asm = args.libame_asm or args.cache / 'libame.asm'
    plug_asm = args.plugin_asm or args.cache / 'plugin.asm'
    if not args.libame_asm:
        disassemble(libame, lib_asm)
    if not args.plugin_asm:
        disassemble(plugin, plug_asm)
    lib_functions = functions(lib_asm, lambda s: s in [BUILTIN, AUDIO, VISITOR] or s.startswith('__ZN7AEffect') and 'Setup' in s)
    plugin_functions = functions(plug_asm, lambda s: s == PLUGIN_INIT or s == '_ACFRegisterComponent')
    paths = {'AVID_EFFECT_BUILTIN_ASM': lib_functions[BUILTIN],
             'AVID_EFFECT_HELPER_ASM': ''.join(v for k, v in lib_functions.items() if 'Setup' in k),
             'AVID_EFFECT_PLUGIN_ASM': plugin_functions[PLUGIN_INIT]}
    for env, data in paths.items():
        path = args.cache / (env + '.asm')
        path.write_text(data)
        os.environ[env] = str(path)
    # Match disassembly instruction bytes against the reviewed ARM64 text.
    # For these images __TEXT's VM and file offsets are both zero.
    for blocks, binary in [(lib_functions, Path(os.environ['AVID_EFFECT_LIBAME'])),
                           (plugin_functions, Path(os.environ['AVID_EFFECT_PLUGIN']))]:
        data = binary.read_bytes()
        for symbol, block in blocks.items():
            for m in re.finditer(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})', block, re.M):
                address = int(m[1], 16)
                assert data[address:address + 4] == bytes.fromhex(m[2]), f'Stale assembly: {symbol}'
    builtins = importlib.import_module('builtin_arm64')
    plugins = importlib.import_module('mceffects_arm64')
    builtin_rows = []
    for alpha in (0, 1):
        for other in (0, 1):
            for ident in range(65536):
                row = builtins.run(ident, alpha, other=other)
                if row.get('name') and row['name'] != 'Unknown Effect':
                    assert row.get('category')
                    builtin_rows.append(row)
    plugin_rows = plugins.run(0) + plugins.run(1)
    translations = []
    for locale in ['de_DE', 'es_ES', 'fr_FR', 'it_IT', 'ja_JA', 'zh-cn', 'ru-ru']:
        path = args.install / f'SupportingFiles/International/xml/MCStrings_{locale}.xml'
        source = path.read_bytes()
        manifest['files'].append({'path': str(path.relative_to(args.install)), 'sha256': sha(source)})
        strings = ET.fromstring(source.decode('utf8').replace('AvidSTX:', ''))
        mapped = {}
        for item in strings.iter('StringTranslation'):
            if item.findtext('SourceFile', '').endswith('AEffectInfo.c'):
                name, value = item.findtext('Key', ''), item.findtext('Value', '')
                if value and name not in mapped:
                    mapped[name] = value
        translations.append(mapped)
    rows, seen = [], set()
    def add(row, source):
        key = row['name'], row['category']
        if key in seen:
            return
        seen.add(key)
        rows.append({'name': key[0], 'category': key[1], 'source': source,
                     'localised': [t.get(key[0], '') for t in translations] if source != 'shipped-third-party-registry' else []})
    for row in builtin_rows:
        add(row, 'libame-registration')
    for row in plugin_rows:
        if row['identifier'] != 'FXBaseProxyRegistration':
            add(row, 'MCEffects-registration')
    # Additional exact render names reviewed against current code/resources.
    assert '"Audio Dissolve"' in lib_functions[AUDIO] and '"Blend"' in lib_functions[AUDIO]
    assert '"Motion Effect"' in lib_functions[VISITOR] and '"AudioSuite"' in lib_functions[VISITOR]
    dverb_help = args.install / 'Help/Content/Editing_Guide/D_Verb__Audio_Track_Effect_and_AudioSuit.htm'
    assert 'D-Verb (Audio Track Effect and AudioSuite)' in dverb_help.read_text()
    manifest['files'].append({'path': str(dverb_help.relative_to(args.install)), 'sha256': sha(dverb_help.read_bytes())})
    for name, category in [('Audio Dissolve', 'Blend'), ('Motion Effect', 'Timewarp'), ('D-Verb', 'AudioSuite')]:
        add({'name': name, 'category': category}, 'reviewed-render-name')
    registry = app / 'Contents/Resources/Default_ExternalDynamicAVX2.xml'
    manifest['files'].append({'path': str(registry.relative_to(args.install)), 'sha256': sha(registry.read_bytes())})
    external_rows = [{'name': e.findtext('Name'), 'category': e.findtext('ClassName'),
                      'effectId': e.findtext('EffectID')} for e in ET.fromstring(registry.read_bytes())]
    for row in external_rows:
        assert row['name'] and row['category']
        add(row, 'shipped-third-party-registry')
    manifest['counts'] = {'builtinRegistrationIds': len({r['id'] for r in builtin_rows}),
                          'builtinNameCategoryPairs': len({(r['name'], r['category']) for r in builtin_rows}),
                          'MCEffectsRecordsPerGateState': len(plugin_rows) // 2,
                          'thirdPartyRegistryRecords': len(external_rows), 'compiledNameCategoryPairs': len(rows)}
    manifest['functions'] = {BUILTIN: '0x7467e0', AUDIO: '0x755de0', VISITOR: '0xa86d48',
                             PLUGIN_INIT: '0x411e0', 'MCEffects::_ACFRegisterComponent': '0xb694'}
    evidence = ROOT / 'docs/evidence/avid-effects-26.8'
    evidence.mkdir(parents=True, exist_ok=True)
    for name, obj in [('manifest', manifest), ('catalogue', rows), ('builtin-registrations', builtin_rows),
                      ('plugin-registrations', plugin_rows), ('external-registry', external_rows)]:
        (evidence / (name + '.json')).write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n')
    def literal(value):
        return json.dumps(value, ensure_ascii=False)
    output = ['// Generated by tools/avid_effects/extract.py; see docs/evidence/avid-effects-26.8/README.md.',
              '// Distinct name/category pairs, including both AlphaFlex states. No plug-in availability claim.']
    for row in rows:
        output.append('\t\t{' + literal(row['name']) + ', ' + literal(row['category']) + ', {' +
                      ', '.join(literal(x) for x in row['localised']) + '}},')
    (ROOT / 'src/avideffectscatalogue.inc').write_text('\n'.join(output) + '\n')
    print(json.dumps(manifest['counts'], indent=2))

if __name__ == '__main__':
    main()
