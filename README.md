Zhparser
========

Zhparser is a PostgreSQL extension for full-text search of Chinese.It implements a Chinese parser base on 
the Simple Chinese Word Segmentation(SCWS).

Project home page：http://amutu.com/blog/zhparser/

INSTALL
-------
1.安装SCWS

```
 wget -q -O - http://www.xunsearch.com/scws/down/scws-1.2.2.tar.bz2 | tar xf -

 cd scws-1.2.2 ; ./configure ; make install

注意:在FreeBSD release 10及以上版本上运行configure时，需要增加--with-pic选项。
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
