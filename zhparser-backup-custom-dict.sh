if [ $# -lt 2 ];then
	echo "usage: $0 <backup|restore|delete> <pgdata_dir> [restore_from_dir]"
	echo "warning: delete is a dangerous cmd, it will delete your custom from pgdata_dir."
       	echo "!!!you should run backup cmd first, then run the delete cmd !!!"
	exit 2
fi
cmd=$1
pgdata=$2
restore_from_dir=$3

if [ $cmd = 'backup' ];then
	backup_dir=zhparser-backup-custom-dict-$(date +'%F:%T')
	mkdir ./$backup_dir
	echo "will backup $pgdata/base/zhprs_dict_* to $backup_dir/"
	cp -a $pgdata/base/zhprs_dict_* $backup_dir/
	if [ "$?" -ne 0 ];
	then
		echo "backup error!"
		exit 1
	else
		echo "backup ok!"
	fi
fi

if [ $cmd = 'delete' ];then
	echo "will delete $pgdata/base/zhprs_dict_*"
	rm $pgdata/base/zhprs_dict_*
	if [ "$?" -ne 0 ];
	then
		echo "delete error!"
		exit 1
	else
		echo "delete ok!"
	fi
fi

if [ $cmd = 'restore' ];then
	echo "will restore $restore_from_dir/zhprs_dict_* to $pgdata/base/"
	cp -a $restore_from_dir/zhprs_dict_* $pgdata/base/
	if [ "$?" -ne 0 ];
	then
		echo "restore error!"
		exit 1
	else
		echo "restore ok!"
	fi
fi
