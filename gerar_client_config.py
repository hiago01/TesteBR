#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gerar_client_config.py - le o launcher.apk novo e gera/atualiza o client_config.json do servidor HTTP,
com o HASH de verificacao (SHA-256) que o launcher confere antes de instalar.

  # 1) ler o APK (so mostra: pacote, versionCode, versionName, tamanho, sha256). Nao altera nada.
  python gerar_client_config.py ler  --apk C:\\cache-server\\launcher.apk

  # 2) gerar/atualizar o client_config.json (mantem url_cache_files, packs, version e qualquer outro campo)
  python gerar_client_config.py gerar --apk C:\\cache-server\\launcher.apk ^
         --config C:\\cache-server\\client_config.json ^
         --url-launcher https://maximoroleplay.com.br/launcher.apk

  # 3) conferir o que o SERVIDOR esta servindo (baixa o client_config e o APK pela URL e compara o hash)
  python gerar_client_config.py conferir --base-url https://maximoroleplay.com.br

Como o launcher usa (UpdateService.java):
  * version_code  : se for DIFERENTE do versionCode do app instalado, o jogo exige atualizar (aviso obrigatorio,
                    sem botao de cancelar/continuar) e baixa url_launcher.
  * launcher_sha256 / launcher_size : conferidos DEPOIS do download; se nao baterem, o arquivo e apagado e
                    baixado de novo (3 tentativas); so entao o instalador do Android e aberto.
  Sem esses dois campos (client_config antigo) o launcher continua funcionando, apenas sem a conferencia.

version_code vem do proprio APK (AndroidManifest binario) - nao precisa digitar; --version-code so para forcar outro valor.
IMPORTANTE: o APK novo precisa estar assinado com a MESMA chave (keystore) do instalado, senao o Android recusa a
atualizacao; e o versionCode do APK novo deve ser o mesmo que vai em version_code (este script garante isso).
So usa a biblioteca padrao do Python.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import tempfile
import urllib.request
import zipfile

PACKAGE_ESPERADO = "com.samp.mobile"
ATTR_VERSION_CODE = 0x0101021B     # android:versionCode
ATTR_VERSION_NAME = 0x0101021C     # android:versionName

# ---------------------------------------------------------------------------------------------- AXML (manifest binario)
RES_STRING_POOL = 0x0001
RES_XML_START_ELEMENT = 0x0102
RES_XML_RESOURCE_MAP = 0x0180


def _read_string_pool(data, off):
    # cabecalho: type(2) hsize(2) size(4) strCount(4) styleCount(4) flags(4) strStart(4) styleStart(4)
    _t, hsize, size, n, _ns, flags, str_start, _ss = struct.unpack_from("<HHIIIIII", data, off)
    utf8 = bool(flags & 0x100)
    offs = struct.unpack_from("<%dI" % n, data, off + hsize)
    base = off + str_start
    out = []
    for o in offs:
        p = base + o
        if utf8:
            def rd8(p):
                l = data[p]; p += 1
                if l & 0x80:
                    l = ((l & 0x7F) << 8) | data[p]; p += 1
                return l, p
            _chars, p = rd8(p)
            blen, p = rd8(p)
            out.append(data[p:p + blen].decode("utf-8", "replace"))
        else:
            l = struct.unpack_from("<H", data, p)[0]; p += 2
            if l & 0x8000:
                l = ((l & 0x7FFF) << 16) | struct.unpack_from("<H", data, p)[0]; p += 2
            out.append(data[p:p + l * 2].decode("utf-16-le", "replace"))
    return out


def ler_manifest(apk_path):
    """Devolve {package, versionCode, versionName} do AndroidManifest.xml binario do APK."""
    with zipfile.ZipFile(apk_path) as z:
        data = z.read("AndroidManifest.xml")
    if struct.unpack_from("<H", data, 0)[0] != 0x0003:
        raise ValueError("AndroidManifest.xml nao e XML binario do Android")
    pos = struct.unpack_from("<H", data, 2)[0]
    strings, resmap = [], []
    info = {}
    while pos + 8 <= len(data):
        t, hsize, size = struct.unpack_from("<HHI", data, pos)
        if size == 0:
            break
        if t == RES_STRING_POOL:
            strings = _read_string_pool(data, pos)
        elif t == RES_XML_RESOURCE_MAP:
            resmap = list(struct.unpack_from("<%dI" % ((size - hsize) // 4), data, pos + hsize))
        elif t == RES_XML_START_ELEMENT:
            # nó: header(16) + ns(4) name(4) attrStart(2) attrSize(2) attrCount(2) idIdx(2) classIdx(2) styleIdx(2)
            name_i = struct.unpack_from("<i", data, pos + 20)[0]
            attr_start, attr_size, attr_count = struct.unpack_from("<HHH", data, pos + 24)
            if name_i >= 0 and strings[name_i] == "manifest":
                a0 = pos + 16 + attr_start
                for i in range(attr_count):
                    ap = a0 + i * attr_size
                    _ns, an, raw, _tsz, _res0, dtype, dval = struct.unpack_from("<iiiHBBi", data, ap)
                    aname = strings[an] if an >= 0 else ""
                    aid = resmap[an] if 0 <= an < len(resmap) else 0
                    if aname == "package" or (raw >= 0 and aname == "package"):
                        info["package"] = strings[raw] if raw >= 0 else ""
                    elif aid == ATTR_VERSION_CODE or aname == "versionCode":
                        info["versionCode"] = dval & 0xFFFFFFFF
                    elif aid == ATTR_VERSION_NAME or aname == "versionName":
                        info["versionName"] = strings[raw] if raw >= 0 else ""
                break
        pos += size
    if "versionCode" not in info:
        raise ValueError("versionCode nao encontrado no manifest do APK")
    info.setdefault("package", "")
    info.setdefault("versionName", "")
    return info


def sha256_arquivo(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def info_apk(apk):
    if not os.path.isfile(apk):
        sys.exit("ERRO: APK nao encontrado: %s" % apk)
    try:
        if not zipfile.is_zipfile(apk):
            sys.exit("ERRO: %s nao e um APK/ZIP valido (arquivo cortado?)" % apk)
        m = ler_manifest(apk)
    except (ValueError, KeyError, struct.error) as e:
        sys.exit("ERRO ao ler o manifest do APK: %s" % e)
    m["size"] = os.path.getsize(apk)
    m["sha256"] = sha256_arquivo(apk)
    return m


def mostrar(m):
    print("pacote      :", m["package"])
    print("versionCode :", m["versionCode"])
    print("versionName :", m["versionName"])
    print("tamanho     :", m["size"], "bytes (%.1f MB)" % (m["size"] / 1048576))
    print("sha256      :", m["sha256"])


# ---------------------------------------------------------------------------------------------- comandos
def cmd_ler(a):
    m = info_apk(a.apk)
    mostrar(m)
    if m["package"] != PACKAGE_ESPERADO:
        print("AVISO: pacote diferente de %s" % PACKAGE_ESPERADO)
    return 0


def cmd_gerar(a):
    m = info_apk(a.apk)
    mostrar(m)
    if m["package"] != PACKAGE_ESPERADO:
        sys.exit("ERRO: o APK e do pacote '%s', o launcher instalado e '%s'. Nada foi gravado." % (m["package"], PACKAGE_ESPERADO))
    vc = a.version_code if a.version_code is not None else m["versionCode"]
    if vc != m["versionCode"]:
        sys.exit("ERRO: --version-code %d difere do versionCode do APK (%d). O launcher ficaria em loop de "
                 "atualizacao. Nada foi gravado." % (vc, m["versionCode"]))
    cfg = {}
    if os.path.isfile(a.config):
        with open(a.config, "r", encoding="utf-8-sig") as f:
            try:
                cfg = json.load(f)
            except json.JSONDecodeError as e:
                sys.exit("ERRO: %s nao e um JSON valido (%s). Nada foi gravado." % (a.config, e))
    cc = cfg.setdefault("client_config", {})
    if not a.url_launcher and not cc.get("url_launcher"):
        sys.exit("ERRO: informe --url-launcher (o client_config ainda nao tem url_launcher).")
    antigo = cc.get("version_code")
    if a.url_launcher:
        cc["url_launcher"] = a.url_launcher
    if not str(cc["url_launcher"]).lower().startswith(("http://", "https://")):
        sys.exit("ERRO: url_launcher precisa comecar com http:// ou https://")
    cc["version_code"] = vc
    cc["launcher_sha256"] = m["sha256"]
    cc["launcher_size"] = m["size"]
    if "url_cache_files" not in cc:
        print("AVISO: client_config sem url_cache_files (o launcher exige esse campo).")
    out = a.out or a.config
    tmp = out + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, out)
    print("\nGravado %s  (version_code %s -> %d)" % (out, antigo, vc))
    print("Copie o APK para a URL:", cc["url_launcher"])
    if antigo is not None and antigo == vc:
        print("AVISO: version_code igual ao anterior; quem ja tem esta versao nao sera atualizado.")
    return 0


def _get(url, dest=None, timeout=30):
    req = urllib.request.Request(url, headers={"Accept-Encoding": "identity", "User-Agent": "gerar_client_config/1"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        if dest is None:
            return r.read()
        with open(dest, "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b:
                    break
                f.write(b)


def cmd_conferir(a):
    cfg_url = a.base_url.rstrip("/") + "/client_config.json"
    try:
        cfg = json.loads(_get(cfg_url).decode("utf-8-sig"))["client_config"]
    except Exception as e:  # noqa: BLE001 - mensagem direta ao operador
        sys.exit("ERRO ao ler %s: %s" % (cfg_url, e))
    print("client_config:", cfg_url)
    print("  version_code    :", cfg.get("version_code"))
    print("  url_launcher    :", cfg.get("url_launcher"))
    print("  launcher_sha256 :", cfg.get("launcher_sha256", "(ausente)"))
    print("  launcher_size   :", cfg.get("launcher_size", "(ausente)"))
    fd, tmp = tempfile.mkstemp(suffix=".apk")
    os.close(fd)
    ok = True
    try:
        _get(cfg["url_launcher"], tmp, timeout=120)
        m = info_apk(tmp)
        print("APK baixado:")
        mostrar(m)
        if m["versionCode"] != cfg.get("version_code"):
            print("FALHA: versionCode do APK (%s) != version_code do client_config (%s) -> loop de atualizacao" % (m["versionCode"], cfg.get("version_code"))); ok = False
        if cfg.get("launcher_sha256") and cfg["launcher_sha256"].lower() != m["sha256"]:
            print("FALHA: sha256 do client_config nao bate com o APK servido"); ok = False
        if cfg.get("launcher_size") and int(cfg["launcher_size"]) != m["size"]:
            print("FALHA: launcher_size nao bate com o APK servido"); ok = False
        if not cfg.get("launcher_sha256"):
            print("AVISO: client_config sem launcher_sha256 -> o launcher nao confere o APK. Rode o comando 'gerar'.")
    except Exception as e:  # noqa: BLE001
        print("FALHA ao baixar/ler o APK:", e); ok = False
    finally:
        os.path.exists(tmp) and os.remove(tmp)
    print("RESULTADO:", "OK" if ok else "COM PROBLEMAS")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("ler", help="mostra versionCode/sha256 do APK")
    s.add_argument("--apk", required=True)
    s.set_defaults(fn=cmd_ler)
    s = sub.add_parser("gerar", help="grava version_code, launcher_sha256 e launcher_size no client_config.json")
    s.add_argument("--apk", required=True)
    s.add_argument("--config", required=True, help="client_config.json (criado se nao existir)")
    s.add_argument("--out", help="saida (padrao: sobrescreve --config)")
    s.add_argument("--url-launcher", help="URL publica do APK (ex.: http://IP:8080/launcher.apk)")
    s.add_argument("--version-code", type=int, help="so para conferir; precisa ser igual ao do APK")
    s.set_defaults(fn=cmd_gerar)
    s = sub.add_parser("conferir", help="confere o que o servidor HTTP esta servindo")
    s.add_argument("--base-url", required=True)
    s.set_defaults(fn=cmd_conferir)
    a = ap.parse_args()
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
