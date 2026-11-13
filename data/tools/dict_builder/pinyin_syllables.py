"""Canonical readings, distinct from abbreviated, fuzzy and Shuangpin spellings.

The annotated rime-ice inventory includes rare readings such as biang and fiao.
Syllabic interjections m/n/ng are valid readings too. Write umlaut vowels as v;
lue/nue and other input aliases belong in the spelling schema, not this table.
"""

import sqlite3
from contextlib import closing


SYLLABLE_GROUPS = (
    "a ai an ang ao",
    "ba bai ban bang bao bei ben beng bi bian biang biao bie bin bing bo bu",
    "ca cai can cang cao ce cei cen ceng",
    "cha chai chan chang chao che chen cheng chi chong chou chu chua chuai chuan "
    "chuang chui chun chuo",
    "ci cong cou cu cuan cui cun cuo",
    "da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou "
    "du duan dui dun duo",
    "e ei en eng er",
    "fa fan fang fei fen feng fiao fo fou fu",
    "ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo",
    "ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo",
    "ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun",
    "ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui kun kuo",
    "la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu lo long "
    "lou lu luan lun luo lv lve",
    "m ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu",
    "n na nai nan nang nao ne nei nen neng ng ni nia nian niang niao nie nin ning "
    "niu nong nou nu nuan nun nuo nv nve",
    "o ou",
    "pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu",
    "qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun",
    "ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo",
    "sa sai san sang sao se sen seng",
    "sha shai shan shang shao she shei shen sheng shi shou shu shua shuai shuan "
    "shuang shui shun shuo",
    "si song sou su suan sui sun suo",
    "ta tai tan tang tao te tei teng ti tian tiao tie ting tong tou tu tuan tui tun tuo",
    "wa wai wan wang wei wen weng wo wu",
    "xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun",
    "ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun",
    "za zai zan zang zao ze zei zen zeng",
    "zha zhai zhan zhang zhao zhe zhei zhen zheng zhi zhong zhou zhu zhua zhuai "
    "zhuan zhuang zhui zhun zhuo",
    "zi zong zou zu zuan zui zun zuo",
)
CANONICAL_SYLLABLES = frozenset(" ".join(SYLLABLE_GROUPS).split())


def validate_syllables(code, syllable_ids):
    parts = syllable_ids.split(":") if isinstance(syllable_ids, str) else []
    if not parts or "".join(parts) != code or any(
        part not in CANONICAL_SYLLABLES for part in parts
    ):
        raise ValueError(f"invalid syllable annotation: {code!r}, {syllable_ids!r}")
    return parts


def validate_dictionary(database_path):
    """Validate before opening outputs; return the actual dictionary inventory."""
    syllabary = set()
    with closing(sqlite3.connect(database_path)) as connection:
        columns = {row[1] for row in connection.execute("PRAGMA table_info(dict)")}
        if "syllable_ids" not in columns:
            raise ValueError("Pinyin dictionary requires explicit syllable_ids")
        for row_id, text, code, annotation in connection.execute(
            "SELECT rowid, text, code, syllable_ids FROM dict"
        ):
            try:
                syllabary.update(validate_syllables(code, annotation))
            except ValueError as error:
                raise ValueError(f"dictionary row {row_id}, {text!r}: {error}") from error
    return syllabary
