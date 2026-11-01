#!/usr/bin/env python3
"""Generate colon-separated Pinyin syllable IDs for a SQLite dictionary.

Preserves explicit syllable boundaries. Missing or malformed annotations are
repaired only when the continuous code has one complete, unambiguous parse.
"""

import sys
import sqlite3

# Standard pinyin syllables (from cxx-ime PinyinSegmentor), sorted by length desc
SYLLABLES_RAW = """
chuang shuang zhuang shuai zhuai zhang chang shang zheng sheng zhong chong
bang dang fang gang hang kang lang mang nang pang rang sang tang wang yang
bian dian jian lian mian nian pian qian tian xian
biao diao jiao liao miao niao piao tiao xiao
bing ding jing ling ming ning ping qing ting xing
chai chuai guai huai kuai shuai zhai
chan chuan duan guan huan juan kuan luan nuan quan ruan suan tuan yuan zhuan
chao jiao liao miao niao piao qiao tiao xiao biao diao
chen shen zhen
cheng deng geng heng keng leng meng neng peng reng sheng teng zeng zheng
chua hua kua shua zhua
chui dui gui hui kui rui sui tui zui zhui cui
chun cun dun gun hun jun kun lun run shun sun tun xun yun zhun
chuo cuo duo guo huo kuo luo nuo ruo shuo tuo zuo zhuo
cong dong gong hong kong long nong rong song tong yong zhong
chou dou fou gou hou kou lou mou nou pou rou shou sou tou you zhou
chuan duan guan huan juan kuan luan nuan quan ruan suan tuan yuan zhuan
chui dui gui hui kui rui sui tui zui zhui
guai huai kuai shuai chuai zhuai
chang shang zhang
chao jiao liao miao niao piao qiao tiao xiao
che de ge he ke le me ne re se she te ye ze zhe
chi ci di li mi ni pi qi ri shi si ti xi yi zi zhi
chu du fu gu hu ju ku lu mu nu pu qu ru shu su tu wu xu yu zu zhu
chuai guai huai kuai shuai zhuai
chun cun dun gun hun jun kun lun run shun sun tun xun yun zhun
cong dong gong hong kong long nong rong song tong yong zhong
cou dou fou gou hou kou lou mou nou pou rou shou sou tou you zhou
cui dui gui hui kui rui sui tui zui zhui
dai gai hai kai lai mai nai pai sai tai wai zai zhai
dan fan gan han kan lan man nan pan ran san tan wan yan zan zhan
dao gao hao kao lao mao nao pao rao sao tao yao zao zhao
dia lia jia qia xia
dian jian lian mian nian pian qian tian xian
diao jiao liao miao niao piao qiao tiao xiao
die jie lie mie nie pie qie tie xie
ding jing ling ming ning ping qing ting xing
diu jiu liu miu niu qiu xiu
dui gui hui kui rui sui tui zui zhui
duo guo huo kuo luo nuo ruo shuo tuo zuo zhuo
fei gei hei kei lei mei nei pei shei tei wei zei zhei
fen gen hen ken men nen pen ren sen shen wen zen zhen
feng geng heng keng leng meng neng peng reng sheng teng zeng zheng
gua hua kua shua zhua
guai huai kuai shuai zhuai
guan huan juan kuan luan nuan quan ruan suan tuan yuan zhuan
gui hui kui rui sui tui zui zhui
gun hun jun kun lun run shun sun tun xun yun zhun
guo huo kuo luo nuo ruo shuo tuo zuo zhuo
heng keng leng meng neng peng reng sheng teng zeng zheng
hou kou lou mou nou pou rou shou sou tou you zhou
hua kua shua zhua
huai kuai shuai zhuai
huan juan kuan luan nuan quan ruan suan tuan yuan zhuan
hui kui rui sui tui zui zhui
hun jun kun lun run shun sun tun xun yun zhun
huo kuo luo nuo ruo shuo tuo zuo zhuo
jia lia qia xia
jian lian mian nian pian qian tian xian
jiao liao miao niao piao qiao tiao xiao
jie lie mie nie pie qie tie xie
jing ling ming ning ping qing ting xing
jiu liu miu niu qiu xiu
juan kuan luan nuan quan ruan suan tuan yuan zhuan
jue lue nue qu xue yue
jun kun lun run shun sun tun xun yun zhun
kai lai mai nai pai sai tai wai zai zhai
kan lan man nan pan ran san tan wan yan zan zhan
kang lang mang nang pang rang sang tang wang yang zhang
kao lao mao nao pao rao sao tao yao zao zhao
kei lei mei nei pei shei tei wei zei zhei
ken men nen pen ren sen shen wen zen zhen
keng leng meng neng peng reng sheng teng zeng zheng
kou lou mou nou pou rou shou sou tou you zhou
kua shua zhua hua
kuai shuai zhuai huai guai
kuan luan nuan quan ruan suan tuan yuan zhuan
kui rui sui tui zui zhui hui gui dui
kun lun run shun sun tun xun yun zhun hun jun gun dun cun
kuo luo nuo ruo shuo tuo zuo zhuo huo guo duo cuo
lai mai nai pai sai tai wai zai zhai gai hai kai
lan man nan pan ran san tan wan yan zan zhan gan han kan
lang mang nang pang rang sang tang wang yang zhang gang hang kang
lao mao nao pao rao sao tao yao zao zhao gao hao kao
lei mei nei pei shei tei wei zei zhei gei hei kei
leng meng neng peng reng sheng teng zeng zheng geng heng keng
lia jia qia xia
lian mian nian pian qian tian xian jian dian bian
liao miao niao piao qiao tiao xiao jiao diao biao
lie mie nie pie qie tie xie jie die bie
ling ming ning ping qing ting xing jing ding bing
liu miu niu qiu xiu jiu diu
lou mou nou pou rou shou sou tou you zhou dou fou gou hou kou
luan nuan quan ruan suan tuan yuan zhuan huan juan kuan duan guan
lue nue qu xue yue jue
luo nuo ruo shuo tuo zuo zhuo huo guo duo cuo
mai nai pai sai tai wai zai zhai gai hai kai
man nan pan ran san tan wan yan zan zhan gan han kan
mang nang pang rang sang tang wang yang zhang gang hang kang
mao nao pao rao sao tao yao zao zhao gao hao kao
mei nei pei shei tei wei zei zhei gei hei kei lei
men nen pen ren sen shen wen zen zhen gen hen ken
meng neng peng reng sheng teng zeng zheng geng heng keng
mie nie pie qie tie xie jie die lie
miao niao piao qiao tiao xiao jiao diao liao biao
ming ning ping qing ting xing jing ding ling bing
miu niu qiu xiu jiu diu liu
mou nou pou rou shou sou tou you zhou dou fou gou hou kou lou
nai pai sai tai wai zai zhai gai hai kai lai mai
nan pan ran san tan wan yan zan zhan gan han kan lan man
nang pang rang sang tang wang yang zhang gang hang kang lang mang
nao pao rao sao tao yao zao zhao gao hao kao lao mao
nei pei shei tei wei zei zhei gei hei kei lei mei
nen pen ren sen shen wen zen zhen gen hen ken men
neng peng reng sheng teng zeng zheng geng heng keng leng meng
nian pian qian tian xian jian lian mian dian bian
niao piao qiao tiao xiao jiao diao miao liao biao
nie pie qie tie xie jie die mie lie
ning ping qing ting xing jing ding ling ming bing
niu qiu xiu jiu diu liu miu
nou pou rou shou sou tou you zhou dou fou gou hou kou lou mou
nuan quan ruan suan tuan yuan zhuan huan juan kuan duan guan luan
nue qu xue yue jue lue
nuo ruo shuo tuo zuo zhuo huo guo duo cuo luo
pai sai tai wai zai zhai gai hai kai lai mai nai
pan ran san tan wan yan zan zhan gan han kan lan man nan
pang rang sang tang wang yang zhang gang hang kang lang mang nang
pao rao sao tao yao zao zhao gao hao kao lao mao nao
pei shei tei wei zei zhei gei hei kei lei mei nei
pen ren sen shen wen zen zhen gen hen ken men nen
peng reng sheng teng zeng zheng geng heng keng leng meng neng
pian qian tian xian jian lian mian nian dian bian
piao qiao tiao xiao jiao diao miao niao liao biao
pie qie tie xie jie die mie nie lie
ping qing ting xing jing ding ling ming ning bing
qiu xiu jiu diu liu miu niu
quan ruan suan tuan yuan zhuan huan juan kuan luan nuan duan guan
que xue yue jue lue nue
ran san tan wan yan zan zhan gan han kan lan man nan pan
rang sang tang wang yang zhang gang hang kang lang mang nang pang
rao sao tao yao zao zhao gao hao kao lao mao nao pao
reng sheng teng zeng zheng geng heng keng leng meng neng peng
ren sen shen wen zen zhen gen hen ken men nen pen
rong song tong yong zhong cong dong gong hong kong long nong
rou shou sou tou you zhou dou fou gou hou kou lou mou nou pou
ruan suan tuan yuan zhuan huan juan kuan luan nuan duan guan quan
rui sui tui zui zhui hui kui gui dui cui
run shun sun tun xun yun zhun hun jun kun gun dun cun lun
ruo shuo tuo zuo zhuo huo guo duo cuo luo nuo
sang tang wang yang zhang gang hang kang lang mang nang pang rang
sao tao yao zao zhao gao hao kao lao mao nao pao rao
sen shen wen zen zhen gen hen ken men nen pen ren
sheng teng zeng zheng geng heng keng leng meng neng peng reng
shei tei wei zei zhei gei hei kei lei mei nei pei
shou sou tou you zhou dou fou gou hou kou lou mou nou pou rou
shua zhua hua kua gua
shuai zhuai huai kuai guai chuai
shun sun tun xun yun zhun hun jun kun gun dun cun lun run
shuo tuo zuo zhuo huo guo duo cuo luo nuo ruo
song tong yong zhong cong dong gong hong kong long nong rong
sou tou you zhou dou fou gou hou kou lou mou nou pou rou shou
suan tuan yuan zhuan huan juan kuan luan nuan duan guan quan ruan
sui tui zui zhui hui kui gui dui cui rui
sun tun xun yun zhun hun jun kun gun dun cun lun run shun
tian xian jian lian mian nian pian qian dian bian
tiao xiao jiao diao miao niao piao qiao liao biao
tie xie jie die mie nie pie qie lie
tong yong zhong cong dong gong hong kong long nong rong song
tou you zhou dou fou gou hou kou lou mou nou pou rou shou sou
tuan yuan zhuan huan juan kuan luan nuan quan ruan suan duan guan
tui zui zhui hui kui gui dui cui rui sui
tun xun yun zhun hun jun kun gun dun cun lun run shun sun
tuo zuo zhuo huo guo duo cuo luo nuo ruo shuo
wang yang zhang gang hang kang lang mang nang pang rang sang tang
wei zei zhei gei hei kei lei mei nei pei shei tei
wen zen zhen gen hen ken men nen pen ren sen shen
xia jia lia qia
xian jian lian mian nian pian qian tian dian bian
xiao jiao diao miao niao piao qiao tiao liao biao
xie jie die mie nie pie qie tie lie
xing jing ding ling ming ning ping qing ting bing
xiu jiu diu liu miu niu qiu
xue yue jue lue nue que
xun yun zhun hun jun kun gun dun cun lun run shun sun tun
yang zhang gang hang kang lang mang nang pang rang sang tang wang
yao zao zhao gao hao kao lao mao nao pao rao sao tao
yuan zhuan huan juan kuan luan nuan quan ruan suan tuan duan guan
yun zhun hun jun kun gun dun cun lun run shun sun tun xun
zan zhan gan han kan lan man nan pan ran san tan wan yan
zao zhao gao hao kao lao mao nao pao rao sao tao yao
zei zhei gei hei kei lei mei nei pei shei tei wei
zeng zheng geng heng keng leng meng neng peng reng sheng teng
zhen gen hen ken men nen pen ren sen shen wen zen
zheng geng heng keng leng meng neng peng reng sheng teng zeng
zhou dou fou gou hou kou lou mou nou pou rou shou sou tou you
zhuan huan juan kuan luan nuan quan ruan suan tuan yuan duan guan
zhui hui kui gui dui cui rui sui tui zui
zhun hun jun kun gun dun cun lun run shun sun tun xun yun
zhuo huo guo duo cuo luo nuo ruo shuo tuo zuo
zui zhui hui kui gui dui cui rui sui tui
zuo zhuo huo guo duo cuo luo nuo ruo shuo tuo
a ai an ang ao
ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu
ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng chi chong chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu cuan cui cun cuo
da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du duan dui dun duo
e ei en eng er
fa fan fang fei fen feng fo fou fu
ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo
ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo
ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun
ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui kun kuo
la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu long lou lu luan lun luo lv lve
ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu
na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning niu nong nou nu nuan nuo nv nve
o ou
pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu
qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun
ran rang rao re ren reng ri rong rou ru ruan rui run ruo
sa sai san sang sao se sen seng sha shai shan shang shao she shei shen sheng shi shou shu shua shuai shuan shuang shui shun shuo si song sou su suan sui sun suo
ta tai tan tang tao te tei teng ti tian tiao tie ting tong tou tu tuan tui tun tuo
wa wai wan wang wei wen weng wo wu
xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun
ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun
za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei zhen zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi zong zou zu zuan zui zun zuo
"""

def load_syllables():
    seen = set()
    syllables = []
    for s in SYLLABLES_RAW.split():
        s = s.strip().lower()
        if s and s not in seen:
            seen.add(s)
            syllables.append(s)
    syllables.sort(key=lambda x: (-len(x), x))
    return syllables

def segment_code(code, syllables):
    """Return the unique full parse; never invent syllables or guess boundaries."""
    paths = [[] for _ in range(len(code) + 1)]
    paths[-1] = [()]
    for position in range(len(code) - 1, -1, -1):
        for syllable in syllables:
            if not code.startswith(syllable, position):
                continue
            for tail in paths[position + len(syllable)]:
                paths[position].append((syllable,) + tail)
                if len(paths[position]) == 2:
                    break
            if len(paths[position]) == 2:
                break
    if not code or not paths[0]:
        raise ValueError(f"cannot segment pinyin code: {code!r}")
    if len(paths[0]) != 1:
        raise ValueError(f"ambiguous pinyin code requires explicit syllables: {code!r}")
    return list(paths[0][0])


def validate_syllables(code, syllable_ids):
    parts = syllable_ids.split(":")
    # Validate representation, not pronunciation: an old segmentation inventory
    # must not rewrite explicit rare readings such as biang into bi:ang.
    if ("".join(parts) != code or
            any(not part or any(ch < "a" or ch > "z" for ch in part) for part in parts)):
        raise ValueError(f"invalid syllable annotation: {code!r}, {syllable_ids!r}")
    return parts


def update_syllable_ids(db_path):
    """Validate every row before publishing any changes to the dictionary."""
    syllables = load_syllables()
    connection = sqlite3.connect(db_path)
    try:
        columns = {row[1] for row in connection.execute("PRAGMA table_info(dict)")}
        annotation = "syllable_ids" if "syllable_ids" in columns else "NULL"
        rows = connection.execute(f"SELECT id, code, {annotation} FROM dict").fetchall()
        updates = []
        for row_id, code, existing in rows:
            if existing:
                try:
                    validate_syllables(code, existing)
                    continue
                except ValueError:
                    pass
            try:
                replacement = ":".join(segment_code(code, syllables))
            except ValueError as error:
                raise ValueError(f"dictionary row {row_id}: {error}") from error
            updates.append((replacement, row_id))
        with connection:
            connection.execute("BEGIN")
            if "syllable_ids" not in columns:
                connection.execute("ALTER TABLE dict ADD COLUMN syllable_ids TEXT")
            connection.executemany(
                "UPDATE dict SET syllable_ids = ? WHERE id = ?", updates
            )
        return len(updates)
    finally:
        connection.close()

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <dict.db>")
        sys.exit(1)

    try:
        count = update_syllable_ids(sys.argv[1])
    except (ValueError, sqlite3.Error) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
    print(f"Updated {count} rows; existing valid annotations were preserved.")

if __name__ == '__main__':
    main()
