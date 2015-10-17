Zhparser
========

Zhparser is a PostgreSQL extension for full-text search of Chinese.It implements a Chinese parser base on 
the Simple Chinese Word Segmentation(SCWS).

Project home page：http://blog.amutu.com/zhparser/

INSTALL
-------
1.安装SCWS

```
 wget -q -O - http://www.xunsearch.com/scws/down/scws-1.2.2.tar.bz2 | tar xf -

 cd scws-1.2.2 ; ./configure ; make install

注意:在FreeBSD release 10及以上版本上运行configure时，需要增加--with-pic选项。

如果是从github上下载的scws源码需要先运行以下命令生成configure文件： 

 touch README;aclocal;autoconf;autoheader;libtoolize;automake --add-missing

```
2.下载zhparser源码

```
 git clone https://github.com/amutu/zhparser.git

```
3.编译和安装zhparser

```
 SCWS_HOME=/usr/local make && make install

```
注意:在*BSD上编译安装时，使用gmake代替make

4.创建extension

```
 psql dbname superuser -c 'CREATE EXTENSION zhparser'

```

CONFIGURATION
-------
以下配置在PG9.2及以上版本使用,这些选项是用来控制字典加载行为和分词行为的,这些选项都不是必须的,默认都为false(即如果没有在配置文件中设置这些选项，则zhparser的行为与将下面的选项设置为false一致)。

zhparser.punctuation_ignore = f 

zhparser.seg_with_duality = f 

zhparser.dict_in_memory = f 

zhparser.multi_short = f 

zhparser.multi_duality = f 

zhparser.multi_zmain = f 

zhparser.multi_zall = f 

除了zhparser自带的词典，用户可以增加自定义词典，自定义词典的优先级高于自带的词典。自定义词典的文件必须放在share/postgresql/tsearch_data目录中,zhparser根据文件扩展名确定词典的格式类型，.txt扩展名表示词典是文本格式，.xdb扩展名表示这个词典是xdb格式，多个文件使用逗号分隔,词典的分词优先级由低到高,如：  

zhparser.extra_dicts = 'dict_extra.txt,mydict.xdb' 

注意：zhparser.extra_dicts和zhparser.dict_in_memory两个选项需要在backend启动前设置（可以在配置文件中修改然后reload，之后新建连接会生效）,其他选项可以随时在session中设置生效。zhparser的选项与scws相关的选项对应，关于这些选项的含义，可以参考scws的文档：http://www.xunsearch.com/scws/docs.php#libscws  

EXAMPLE
-------
```
-- create the extension

CREATE EXTENSION zhparser;

-- make test configuration using parser

CREATE TEXT SEARCH CONFIGURATION testzhcfg (PARSER = zhparser);

-- add token mapping

ALTER TEXT SEARCH CONFIGURATION testzhcfg ADD MAPPING FOR n,v,a,i,e,l WITH simple;

-- ts_parse

SELECT * FROM ts_parse('zhparser', 'hello world! 2010年保障房建设在全国范围内获全面启动，从中央到地方纷纷加大 了保障房的建设和投入力度 。2011年，保障房进入了更大规模的建设阶段。住房城乡建设部党组书记、部长姜伟新去年底在全国住房城乡建设工作会议上表示，要继续推进保障性安居工程建设。');

-- test to_tsvector

SELECT to_tsvector('testzhcfg','“今年保障房新开工数量虽然有所下调，但实际的年度在建规模以及竣工规模会超以往年份，相对应的对资金的需求也会创历>史纪录。”陈国强说。在他看来，与2011年相比，2012年的保障房建设在资金配套上的压力将更为严峻。');

-- test to_tsquery

SELECT to_tsquery('testzhcfg', '保障房资金压力');
```

自定义词库
-------
** 详解 TXT 词库的写法 (TXT词库目前已兼容 cli/scws_gen_dict 所用的文本词库) ** 

1) 每行一条记录，以 # 或 分号开头的相当于注释，忽略跳过 

2) 每行由4个字段组成，依次为“词语"(由中文字或3个以下的字母合成), "TF", "IDF", "词性"，字段时间用空格或制表符分开，数量不限，可自行对齐以美化 

3) 除“词语”外，其它字段可忽略不写。若忽略，TF和IDF默认值为 1.0 而 词性为 "@" 

4) 由于 TXT 库动态加载（内部监测文件修改时间自动转换成 xdb 存于系统临时目录），故建议TXT词库不要过大 

5) 删除词作法，请将词性设为“!“，则表示该词设为无效，即使在其它核心库中存在该词也视为无效 

注意：自定义词典的格式可以是文本TXT，也可以是二进制的XDB格式。XDB格式效率更高，适合大辞典使用。可以使用scws自带的工具scws-gen-dict将文本词典转换为XDB格式。具体参见：http://www.xunsearch.com/scws/docs.php#utilscws 

COPYRITE
--------

zhparser

Portions Copyright (c) 2012-2013, Jov(amutu@amutu.com)

Permission to use, copy, modify, and distribute this software and its documentation
for any purpose, without fee, and without a written agreement is hereby granted,
provided that the above copyright notice and this paragraph and the following 
two paragraphs appear in all copies.

IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO
PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
