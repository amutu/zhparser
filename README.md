Zhparser
========

Zhparser is a PostgreSQL extension for full-text search of Chinese.It it implement a Chinese parser base the Simple Chinese Word Segmentation(SCWS).

[INSTALL]
wget -q -O - http://www.xunsearch.com/scws/down/scws-1.2.1.tar.bz2 | tar xjf -
cd scws-1.2.1 ; ./configure ; make install
git clone https://github.com/amutu-code/zhparser.git
cd zhparser
wget -q -O - http://www.xunsearch.com/scws/down/scws-dict-chs-utf8.tar.bz2 | tar xjf -
SCWS_HOME=/usr/local make && make install

[EXAMPLE]
CREATE EXTENSION zhparser;

-- make test configuration using parser

CREATE TEXT SEARCH CONFIGURATION testzhcfg (PARSER = zhparser);

ALTER TEXT SEARCH CONFIGURATION testzhcfg ADD MAPPING FOR n,v,a,i,e,l WITH simple;

-- ts_parse

SELECT * FROM ts_parse('zhparser', 'hello world! 2010年保障房建设在全国范围内获全面启动，从中央到地方纷纷加大 了 保 障 房 的 建 设 和 投 入 力
 度 。2011年，保障房进入了更大规模的建设阶段。住房城乡建设部党组书记、部长姜伟新去年底在全国住房城乡建设工作会议上表示，要继续推进保障性安居工
程建设。');

SELECT to_tsvector('testzhcfg','“今年保障房新开工数量虽然有所下调，但实际的年度在建规模以及竣工规模会超以往年份，相对应的对资金的需求也会创历>史纪录。”陈国强说。在他看来，与2011年相比，2012年的保障房建设在资金配套上的压力将更为严峻。');

SELECT to_tsquery('testzhcfg', '保障房资金压力');


[COPYRITE]
zhparser

Portions Copyright (c) 2012-2013, Jov(amutu@amutu.com)

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose, without fee, and without a written agreement
is hereby granted, provided that the above copyright notice and this
paragraph and the following two paragraphs appear in all copies.
