#coding=utf-8

from ast import Param
import os
import sys
import json
import datetime
import hashlib
import re
import argparse

config_path = ""
cfg_version_list = []


def show_warning(str):
    print("Warning: %s[%d]: " % (__file__, sys._getframe(1).f_lineno) + str)


def show_error(str):
    print("Error: %s[%d]: " % (__file__, sys._getframe(1).f_lineno) + str)
    sys.exit(-1)


def vict_read_support(vict_cfg):
    read = vict_cfg['read']
    if read is False:
        return False
    return True


def vict_write_support(vict_cfg):
    write = vict_cfg['write']
    if write is False:
        return False
    return True


def vict_overwrite_support(vict_cfg):
    overwrite = vict_cfg['overwrite']
    if overwrite is False:
        return False
    return True


def vict_read_desc(read_cfg, language='zh'):
    desc = read_cfg['desc']
    if language in desc:
        return desc[language]
    return "N/A"


def vict_read_cmd(read_cfg):
    if 'cmd' not in read_cfg:
        return "N/A"
    return read_cfg['cmd']


def vict_write_desc(write_cfg, language='zh'):
    desc = write_cfg['desc']
    if language in desc:
        return desc[language]
    return "N/A"


def vict_write_cmd(write_cfg):
    if 'cmd' not in write_cfg:
        return "N/A"
    return write_cfg['cmd']


def vict_overwrite_desc(overwrite_cfg, language='zh'):
    desc = overwrite_cfg['desc']
    if language in desc:
        return desc[language]
    return "N/A"


def vict_overwrite_cmd(overwrite_cfg):
    if 'cmd' not in overwrite_cfg:
        return "N/A"
    return overwrite_cfg['cmd']


def check_func_parameter_list(lable, param_desc_list, num):
    if len(param_desc_list) != num:
        show_error("func[\"%s\"] \"parameter_list\" num error" % {lable})
    count = 0
    for param in param_desc_list:
        if 'range' not in param:
            show_error("func[\"%s\"] parameter %d missing \"range\" info" %
                       {lable, count})
        if 'type' not in param:
            show_error("func[\"%s\"] parameter %d missing \"type\" info" %
                       {lable, count})
        if 'desc' not in param:
            show_error("func[\"%s\"] parameter %d missing \"desc\" info" %
                       {lable, count})


def check_func_vict_info(lable, vict_cfg):
    if vict_read_support(vict_cfg):
        read_cfg = vict_cfg['read']
        if 'cmd' not in read_cfg:
            show_error("func[\"%s\"] missing read \"cmd\" info" % {lable})
        if 'desc' not in read_cfg:
            show_error("func[\"%s\"] missing read \"desc\" info" % {lable})
    if vict_write_support(vict_cfg):
        write_cfg = vict_cfg['write']
        if 'cmd' not in write_cfg:
            show_error("func[\"%s\"] missing write \"cmd\" info" % {lable})
        if 'desc' not in write_cfg:
            show_error("func[\"%s\"] missing write \"desc\" info" % {lable})
    if vict_overwrite_support(vict_cfg):
        overwrite_cfg = vict_cfg['overwrite']
        if 'cmd' not in overwrite_cfg:
            show_error("func[\"%s\"] missing overwrite \"cmd\" info" % {lable})
        if 'desc' not in overwrite_cfg:
            show_error("func[\"%s\"] missing overwrite \"desc\" info" %
                       {lable})


def check_func_cfg(func, type, index):
    if 'lable' not in func:
        show_error(type + ": func[" + str(index) + "] missing \"lable\" info")
    if 'desc' not in func:
        show_error(type + ": func[" + func['lable'] +
                   "] missing \"desc\" info")
    if 'parameter_list' not in func:
        show_error(type + ": func[" + func['lable'] +
                   "] missing \"parameter_list\" info")
    if 'parameter_desc' not in func:
        show_error(type + ": func[" + func['lable'] +
                   "] missing \"parameter_desc\" info")
    if 'vict' not in func:
        show_error(type + ": func[" + func['lable'] +
                   "] missing \"vict\" info")
    check_func_parameter_list(func['lable'], func['parameter_desc'],
                              len(func['parameter_list']))
    check_func_vict_info(func['lable'], func['vict'])


def is_top_cfg(cfg, path=None):
    if path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = path

    if 'type' not in cfg:
        show_error(ic_cfg_path + ": Unknown config file type")
    if cfg['type'] == "top_cfg":
        return True
    return False


def is_ic_cfg(cfg, path=None):
    if path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = path

    if 'type' not in cfg:
        show_error(ic_cfg_path + ": Unknown config file type")
    if cfg['type'] == "ic_cfg":
        return True
    return False


def get_ic_cfg_func_num(cfg):
    func_list = cfg['list']
    return len(func_list)


def get_ic_cfg_hash(cfg, num=-1, path=None):
    if path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = path

    cfg_str = ""
    func_list = cfg['list']
    index = 0

    for func in func_list:
        check_func_cfg(func, ic_cfg_path, index)
        cfg_str = cfg_str + func['lable'] + str(func['parameter_list'])
        if vict_read_support(func['vict']):
            cfg_str += vict_read_cmd(func['vict']['read'])
        if vict_write_support(func['vict']):
            cfg_str += vict_write_cmd(func['vict']['write'])
        if vict_overwrite_support(func['vict']):
            cfg_str += vict_overwrite_cmd(func['vict']['overwrite'])
        index = index + 1
        if num > 0 and index >= num:
            break

    hash = hashlib.sha256(bytes(cfg_str, encoding="utf-8")).hexdigest()
    return str(hash)


def parse_ic_cfg_version(version):
    v_info = version.split(":")
    v_version = v_info[0].replace('V', '')
    v_version_info = v_version.split(".")
    v_major = int(v_version_info[0], 10)
    v_minor = int(v_version_info[1], 10)
    v_num = int(v_info[1], 10)
    v_hash = v_info[2]
    return v_major, v_minor, v_num, v_hash


def calculate_new_version(new_info, old_info):
    major = old_info['major']
    minor = old_info['minor']
    v_version = "V%d.%d" % (major, minor)
    v_num = old_info['num']
    v_hash = old_info['hash']

    if (new_info['hash_num'] == old_info['hash']):
        if (new_info['num'] == old_info['num']):
            v_version = "V%d.%d" % (major, minor)
            v_hash = old_info['hash']
        else:
            minor = minor + 1
            if (minor > 20):
                minor = 0
                major = major + 1
            v_version = "V%d.%d" % (major, minor)
            v_num = new_info['num']
            v_hash = new_info['hash']
    else:
        major = major + 1
        minor = 0
        v_version = "V%d.%d" % (major, minor)
        v_num = new_info['num']
        v_hash = new_info['hash']

    return v_version, v_num, v_hash


def get_ic_cfg_version(cfg, path=None):
    if path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = path

    if 'version' not in cfg:
        show_warning(ic_cfg_path + ": missing version")
        version = ""
    else:
        version = cfg['version']

    v_num = get_ic_cfg_func_num(cfg)
    v_hash = get_ic_cfg_hash(cfg, path=ic_cfg_path)
    if version == "":
        v_version = "V1.0"
    else:
        match = re.match("V[0-9]+\.[0-9]+:[0-9]+:[0-9a-f]+", version, flags=0)
        if match is None:
            show_error(ic_cfg_path +
                       ": version format error, you can use: \"V1.0:%d:%s\"" %
                       (v_num, v_hash))
        v_major_old, v_minor_old, v_num_old, v_hash_old = parse_ic_cfg_version(
            version)
        v_hash_num = get_ic_cfg_hash(cfg, v_num_old, ic_cfg_path)
        v_version, v_num, v_hash = calculate_new_version(
            {
                'num': v_num,
                'hash': v_hash,
                'hash_num': v_hash_num
            }, {
                'major': v_major_old,
                'minor': v_minor_old,
                'num': v_num_old,
                'hash': v_hash_old
            })

    version = "%s:%d:%s" % (v_version, v_num, v_hash)
    return version


def get_top_cfg_version(cfg, path=None):
    if path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = path

    version_list = ""
    ic_list = cfg['oplus_chg_ic_list']
    for ic_name in ic_list:
        ic_cfg_file_name = get_ic_cfg_file(ic_name)
        with open(ic_cfg_file_name) as f:
            ic = json.load(f)
            f.close
        version = get_ic_cfg_version(ic, ic_name)
        version_list = version_list + "%s[%s]\n" % (ic['name'], version)
    return version_list


def get_cfg_version(cfg, path=None):
    if path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = path

    if is_ic_cfg(cfg, ic_cfg_path):
        version = get_ic_cfg_version(cfg, ic_cfg_path)
    elif is_top_cfg(cfg, ic_cfg_path):
        version = get_top_cfg_version(cfg, ic_cfg_path)
    else:
        show_error(ic_cfg_path + ": Unknown config file type")
    print(version)


def check_ic_cfg_version(cfg, path=None):
    if path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = path

    if is_ic_cfg(cfg, ic_cfg_path) is False:
        show_error(ic_cfg_path + " is not ic config file")

    if 'version' not in cfg:
        show_error(ic_cfg_path + ": missing version")
    version = cfg['version']

    version_new = get_ic_cfg_version(cfg, ic_cfg_path)
    if version != version_new:
        show_error(ic_cfg_path +
                   ": version error, the correct version should be: \"%s\"" %
                   (version_new))
    return version


def get_ic_cfg_file(name):
    global config_path
    file_name = config_path + "/" + name
    if os.path.isfile(file_name) is False:
        show_error(name + ".json not exist")
    return file_name


def merge_ic_cfg(cfg, merge_file=None, cfg_path=None):
    global cfg_version_list
    if cfg_path == None:
        ic_cfg_path = "config file"
    else:
        ic_cfg_path = cfg_path

    if is_top_cfg(cfg, ic_cfg_path) is False:
        show_error(ic_cfg_path + " is not top config file")

    merge_ic_list = []
    ic_list = cfg['oplus_chg_ic_list']
    for ic_cfg_path in ic_list:
        ic_cfg_file_name = get_ic_cfg_file(ic_cfg_path)
        with open(ic_cfg_file_name) as f:
            ic = json.load(f)
            f.close
        version = check_ic_cfg_version(ic, ic_cfg_path)
        cfg_version_list.append("%s[%s]" % (ic['name'], version))
        merge_ic_list.append(ic)
    cfg['oplus_chg_ic_list'] = merge_ic_list

    if merge_file != None:
        with open(merge_file, 'w') as f:
            json.dump(cfg, f, ensure_ascii=False, indent=4)
            f.close()

    return cfg


def creat_h_file_head(file):
    today = datetime.datetime.today()
    file.write("// SPDX-License-Identifier: GPL-2.0-only\n")
    file.write("/*\n")
    file.write(" * Copyright (C) %d-%d Oplus. All rights reserved.\n" %
               (2022, today.year))
    file.write(" */\n\n")
    file.write("#ifndef __OPLUS_CHG_IC_CFG_H__\n")
    file.write("#define __OPLUS_CHG_IC_CFG_H__\n\n")


def creat_h_file_tail(file):
    file.write("#endif /* __OPLUS_CHG_IC_CFG_H__ */\n")


def creat_enum_var(cfg, file):
    enum_list = cfg['enum_list']
    for enum in enum_list:
        name = enum['name']
        if not name is None:
            name = name + " "
        index = 0
        file.write("enum " + name + "{\n")
        for list in enum['list']:
            file.write("	%s = %d,\n" % (list, index))
            index = index + 1
        file.write("};\n\n")


def creat_ic_func_enum(cfg, file):
    ic_list = cfg['oplus_chg_ic_list']
    base = 0
    file.write("enum oplus_chg_ic_func {")
    for ic in ic_list:
        name = ic['name']
        max_index = ic['max']
        func_list = ic['list']
        index = 0

        if max_index < len(func_list):
            show_error(name + ": Too many func entries")

        file.write("\n	/* %s */\n" % (name))
        for func in func_list:
            check_func_cfg(func, name, index)
            file.write("	%s = %d,\n" % (func['lable'], base + index))
            index = index + 1
        base = base + max_index
    file.write("};\n\n")


def creat_ic_func_def(cfg, file):
    ic_list = cfg['oplus_chg_ic_list']
    base = 0
    for ic in ic_list:
        name = ic['name']
        func_list = ic['list']

        file.write("/* %s */\n" % (name))
        for func in func_list:
            file.write("OPLUS_CHG_IC_FUNC_DEF(%s)(struct oplus_chg_ic_dev *" %
                       (func['lable']))
            para_list = func['parameter_list']
            for para in para_list:
                file.write(", %s" % (para))
            file.write(");\n")
        file.write("\n")


def creat_ic_cfg_hash(file):
    file.write("#define OPLUS_CHG_IC_CFG_VERSION \"")
    for version in cfg_version_list:
        file.write("%s\\n" % (version))
    file.write("\"\n\n")


def func_add_para(type, name):
    match = re.search("\[\]", type, flags=0)
    if match:
        type = type.replace("[]", " " + name + "[]")
        return type
    match = re.search("\*", type, flags=0)
    if match:
        type = type.replace("*", "*" + name)
        return type
    return type + " " + name


def creat_debug_func(func):
    str = ""
    func_lable = func['lable']
    str += "int __attribute__((weak)) ic_debug_%s(struct oplus_chg_ic_dev *ic" % (
        func_lable.lower())
    para_list = func['parameter_list']
    count = 0
    para_num = len(para_list)
    for para in para_list:
        count += 1
        str += ", %s" % (func_add_para(para, "para_%d" % (count)))
    str += ")\n{\n"

    str += "	%s_T func = (ic && ic->get_func) ? ic->get_func(ic, %s) : NULL;\n" % (
        func_lable, func_lable)

    add_debug_logic = func['auto_debug_code']
    para_type = 'Unknown'
    para_desc_list = func['parameter_desc']
    for desc in para_desc_list:
        ptype = desc['type']
        if para_type == 'Unknown':
            para_type = ptype
        elif para_type != ptype:
            add_debug_logic = False

    func_overwrite = vict_overwrite_support(func['vict'])

    if add_debug_logic:
        str += "	struct oplus_chg_ic_func_data *func_data;\n"
        str += "	bool virt_en;\n"
        str += "	bool trace_only;\n"
        if para_num > 0:
            str += "	size_t func_data_size;\n"
            str += "	int *item_data;\n"
        if func_overwrite:
            str += "	struct oplus_chg_ic_overwrite_data *overwrite_data;\n"
            str += "	const void *buf;\n"

    str += "\n	if(!func)\n		return -ENOTSUPP;\n\n"

    if add_debug_logic:
        str += "	virt_en = ic->debug.virt_en || oplus_chg_ic_func_virt_is_enable(ic, %s);\n" % (
            func_lable)
    else:
        str += "	return func(ic"
        count = 0
        for para in para_list:
            count += 1
            str += ", para_%d" % (count)
        str += ")\n"

    str += "}\n"
    return str


def creat_debug_file(cfg, file):
    today = datetime.datetime.today()
    file.write("// SPDX-License-Identifier: GPL-2.0-only\n")
    file.write("/*\n")
    file.write(" * Copyright (C) %d-%d Oplus. All rights reserved.\n" %
               (2022, today.year))
    file.write(" */\n\n")

    ic_list = cfg['oplus_chg_ic_list']
    for ic in ic_list:
        name = ic['name']
        func_list = ic['list']

        for func in func_list:
            file.write(creat_debug_func(func))
            file.write("\n")


def language_check(language):
    if language == 'zh':
        return
    elif language == 'en':
        return
    else:
        show_error("unsupported language: %s" % (language))


def markdown_write_para_info(func, file, language):
    language_check(language)
    if len(func['parameter_list']) > 0:
        if language == 'zh':
            file.write("* 参数\n\n")
            file.write("|序号|参数类型|取值范围|输入/输出|描述|\n")
        elif language == 'en':
            file.write("* Parameter\n\n")
            file.write("|number|type|value range|in/out|describe|\n")
        file.write("|:----:|:----:|:----:|:----:|:----|\n")
        param_count = 0
        for param in func['parameter_list']:
            param_desc = func['parameter_desc'][param_count]
            if 'range' in param_desc:
                param_range = param_desc['range']
            else:
                param_range = "None"
            if 'type' in param_desc:
                param_type = param_desc['type']
            else:
                param_type = "None"
            if 'desc' in param_desc:
                if language not in param_desc['desc']:
                    param_sub_desc = "None"
                else:
                    param_sub_desc = param_desc['desc'][language]
            else:
                param_desc = "None"
            file.write(
                "|%d|%s|%s|%s|%s|\n" %
                (param_count, param, param_range, param_type, param_sub_desc))
            param_count += 1
    else:
        if language == 'zh':
            file.write("* 参数: N/A\n")
        elif language == 'en':
            file.write("* Parameter: N/A\n")


def markdown_write_vict_info(func, base, index, file, language):
    language_check(language)
    vict = func['vict']
    if language == 'zh':
        file.write("* 工具支持\n\n")
        file.write("|操作|是否支持|命令|描述|\n")
    elif language == 'en':
        file.write("* Tool support\n\n")
        file.write("|operation|support|command|describe|\n")
    file.write("|:----:|:----:|:----:|:----:|\n")
    if vict_read_support(vict):
        support = "Y"
        cmd = vict_read_cmd(vict['read']).replace("{id}", str(base + index))
        cmd_desc = vict_read_desc(vict['read'], language)
    else:
        support = "N"
        cmd = "N/A"
        cmd_desc = "N/A"
    file.write("|read|%s|%s|%s|\n" % (support, cmd, cmd_desc))
    if vict_write_support(vict):
        support = "Y"
        cmd = vict_write_cmd(vict['write']).replace("{id}", str(base + index))
        cmd_desc = vict_write_desc(vict['write'], language)
    else:
        support = "N"
        cmd = "N/A"
        cmd_desc = "N/A"
    file.write("|write|%s|%s|%s|\n" % (support, cmd, cmd_desc))
    if vict_overwrite_support(vict):
        support = "Y"
        cmd = vict_overwrite_cmd(vict['overwrite']).replace(
            "{id}", str(base + index))
        cmd_desc = vict_overwrite_desc(vict['overwrite'], language)
    else:
        support = "N"
        cmd = "N/A"
        cmd_desc = "N/A"
    file.write("|overwrite|%s|%s|%s|\n" % (support, cmd, cmd_desc))


def markdown_check_write_info(cfg, file, language):
    ic_list = cfg['oplus_chg_ic_list']
    count = 1
    base = 0

    language_check(language)
    for ic in ic_list:
        name = ic['name']
        max_index = ic['max']
        func_list = ic['list']
        version = ic['version']
        index = 0

        if max_index < len(func_list):
            show_error(name + ": Too many func entries")

        file.write("# %d、%s\n" % (count, name))
        file.write("version: %s\n" % (version))

        for func in func_list:
            check_func_cfg(func, name, index)
            file.write("## %s\n" % (func['lable']))

            if language == 'zh':
                file.write("* 编号: %d\n" % (base + index))
            elif language == 'en':
                file.write("* Number: %d\n" % (base + index))
            if language not in func['desc']:
                desc = "None"
            else:
                desc = func['desc'][language]
            if language == 'zh':
                file.write("* 描述: %s\n" % (desc))
            elif language == 'en':
                file.write("* Describe: %s\n" % (desc))

            markdown_write_para_info(func, file, language)
            markdown_write_vict_info(func, base, index, file, language)

            file.write("\n")
            index = index + 1
        file.write("---\n")
        base = base + max_index
        count += 1


def creat_markdown_file(cfg, file, language='zh'):
    language_check(language)
    markdown_check_write_info(cfg, file, language)


def add_cmd(parser):
    parser.add_argument("cfg_file", help="Configuration file to be parsed")
    parser.add_argument("-m",
                        "--merge",
                        help="Combine all configuration files into one")
    parser.add_argument("-gv",
                        "--get_version",
                        help="Get ic config version",
                        action="store_true")
    parser.add_argument("-hd", "--head", help="Output .h configuration file")
    parser.add_argument("-df", "--debug_file", help="Output .c ic debug file")
    parser.add_argument("-md", "--markdown", help="Output markdown file")
    parser.add_argument("-l", "--language", help="Set the language used")


def main():
    global config_path

    parser = argparse.ArgumentParser(
        description='Parsing the oplus ic profile')
    add_cmd(parser)

    args = parser.parse_args()

    cfg_file = args.cfg_file
    config_path = os.path.dirname(cfg_file)
    with open(cfg_file) as f:
        cfg_data = json.load(f)
        f.close

    if args.get_version:
        get_cfg_version(cfg_data, cfg_file)

    merge_cfg_file = None
    merge_cfg_data = None
    if args.merge != None:
        merge_cfg_file = args.merge
        merge_cfg_data = merge_ic_cfg(cfg_data, merge_cfg_file, cfg_file)

    if args.head != None:
        if merge_cfg_data == None:
            merge_cfg_data = merge_ic_cfg(cfg_data)
        cfg_h_file = args.head
        cfg_out_h = open(cfg_h_file, 'w')
        creat_h_file_head(cfg_out_h)
        creat_enum_var(merge_cfg_data, cfg_out_h)
        creat_ic_func_enum(merge_cfg_data, cfg_out_h)
        creat_ic_func_def(merge_cfg_data, cfg_out_h)
        creat_ic_cfg_hash(cfg_out_h)
        creat_h_file_tail(cfg_out_h)
        cfg_out_h.close

    if args.debug_file != None:
        if merge_cfg_data == None:
            merge_cfg_data = merge_ic_cfg(cfg_data)
        debug_file = args.debug_file
        debug = open(debug_file, 'w')
        creat_debug_file(merge_cfg_data, debug)
        debug.close

    if args.markdown != None:
        if merge_cfg_data == None:
            merge_cfg_data = merge_ic_cfg(cfg_data)
        if args.language != None:
            language = args.language
        else:
            language = 'zh'
        markdown_file = args.markdown
        markdown = open(markdown_file, 'w')
        creat_markdown_file(merge_cfg_data, markdown, language)
        markdown.close


if __name__ == '__main__':
    main()
