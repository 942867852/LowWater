# -*- coding: utf-8 -*-
"""PHASE2-REVIEW · C-13 编号规范化（单遍原子替换，避免 token 碰撞）
规范编号：S0 底座 / S1 稀缺 / S2 NPC / S3 经济 / S4 对话 / S5 社区 / S6 传闻 / S7 工作板 / S8 区域内容 / S9 呈现
底座三部分：S0·A 角色判定 / S0·B 世界时钟 / S0·C 确定性
"""
import re, io, sys

BASE = {1: "S0·A", 2: "S0·B", 12: "S0·C"}

FILES = {
    "design/gdd/systems/00-foundation.md":     ({**BASE, 3: "S1", 5: "S2", 7: "S5"}, None),
    "design/gdd/systems/01-scarcity-loop.md":  ({**BASE, 5: "S1", 6: "S3", 7: "S5", 9: "S6"}, "S2"),
    "design/gdd/systems/02-npc-simulation.md": ({**BASE, 5: "S1", 6: "S2", 7: "S5", 9: "S3"}, None),
    "design/gdd/systems/03-economy-barter.md": ({**BASE, 5: "S1", 6: "S2", 7: "S5", 8: "S4", 9: "S3"}, None),
    "design/gdd/systems/04-dialogue-contract.md": ({**BASE, 5: "S1", 6: "S2", 7: "S5", 8: "S4", 9: "S3"}, None),
    "design/gdd/systems/05-community-creed.md":   ({**BASE, 5: "S1", 6: "S2", 7: "S5", 8: "S4", 9: "S3"}, None),
}

TOKEN = re.compile(r"(?<![0-9A-Za-z_])S(\d+)(?![0-9A-Za-z_])")

for path, (mapping, qmark) in FILES.items():
    with io.open(path, encoding="utf-8") as f:
        lines = f.readlines()
    hits, unmapped = {}, []
    out = []
    for i, line in enumerate(lines, 1):
        def sub(m):
            n = int(m.group(1))
            if n in mapping:
                hits[n] = hits.get(n, 0) + 1
                return mapping[n]
            unmapped.append((i, "S%d" % n))
            return m.group(0)
        new = TOKEN.sub(sub, line)
        if qmark:
            new = new.replace("S?", qmark)
        out.append(new)
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.writelines(out)
    print("== %s" % path)
    print("   replaced: %s" % ", ".join("S%d->%s x%d" % (k, mapping[k], v) for k, v in sorted(hits.items())))
    if unmapped:
        print("   UNMAPPED (left as-is): %s" % unmapped)
