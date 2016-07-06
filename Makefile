all:
#	gcc -o s_reservations_timed sg_cmds_basic2.c  sg_cmds_basic.c  sg_cmds_extra.c  sg_cmds_mmc.c  sg_io_linux.c  sg_lib.c  sg_lib_data.c  sg_persist.c  sg_pt_common.c  sg_pt_linux.c
#	gcc -o s_reservations_timed sg_cmds_basic.c  sg_cmds_extra.c  sg_cmds_mmc.c  sg_io_linux.c  sg_lib.c  sg_lib_data.c  sg_persist.c  sg_pt_common.c  sg_pt_linux.c
	gcc -o s_reservations_timed sg_cmds_basic.c  sg_cmds_extra.c  sg_cmds_mmc.c  sg_lib.c  sg_lib_data.c  sg_persist.c  sg_pt_common.c  sg_pt_linux.c
clean:
	rm s_reservations_timed
