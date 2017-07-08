#!/bin/sh

gcc -g -o ouihn ouihn_common.c config_reader.c ouihn_ccm.c ouihn_concm.c ouihn_condcm.c main.c -pthread -lrt
