#include <stdlib.h>
#include <pthread.h>

#include "cluster-shake.h"

#ifndef FAT_UNUSED
#define FAT_UNUSED 65535
#define FAT_FILE_END 65534
#define FAT_BAD_CLUSTER 65533
#endif

const int NO_SHAKE_JOB = -1;

int shake_analyze_fat(shake_farmer *p_s_f) {
	int i;
	int bi;
	int non_empty = 0;
	int bad_clusters = 0;
	int fat_item;

	for (i = 0; i < p_s_f->p_boot_record->cluster_count; i++) {
		fat_item = p_s_f->fat_item[i];
		if (fat_item == FAT_UNUSED) {
			continue;
		}
		if (fat_item == FAT_BAD_CLUSTER) {
			bad_clusters++;
		}
		if ((non_empty++ % p_s_f->CLUSTER_CHUNK_SIZE) == 0) {
			p_s_f->cluster_chunk_read_beginings[bi++] = i;
		}
	}
	printf("Non empty clusters: %04d\t (bad clusters: %02d)", non_empty, bad_clusters);
	return 0;

}

struct shake_farmer *create_shake_farmer(char* FS_path) {
	struct shake_farmer* tmp = malloc(sizeof (struct shake_farmer));

	// otevru soubor a pro jistotu skocim na zacatek           
	p_file = fopen(FS_path, "r");
	fseek(p_file, 0, SEEK_SET);

	tmp->FS_path = FS_path;
	tmp->file_system = p_file;

	// inicializace boot record
	tmp->p_boot_record = malloc(sizeof (struct boot_record));
	fread(tmp->p_boot_record, sizeof (struct boot_record), 1, tmp->file_system);

	// inicializace a nacteni FAT
	tmp->fat_item = malloc(tmp->p_boot_record->cluster_count * sizeof (unsigned int));
	tmp->offset_fat = ftell(tmp->file_system);
	fread(tmp->fat_item, sizeof (unsigned int), tmp->p_boot_record->cluster_count - tmp->p_boot_record->reserved_cluster_count, tmp->file_system);

	// inicializace cluster mutexu
	tmp->lock_cluster_chunk = malloc(sizeof (pthread_mutex_t));
	pthread_mutex_init(tmp->lock_cluster_chunk, NULL);

	// inicializace a nacteni root directory
	tmp->offset_root_directory = ftell(tmp->file_system);
	tmp->p_root_directory = malloc(sizeof (struct root_directory) * tmp->p_boot_record->root_directory_max_entries_count);
	fread(tmp->p_root_directory, sizeof (struct root_directory), tmp->p_boot_record->root_directory_max_entries_count, tmp->file_system);

	// inicializace a nacteni datovych clusteru
	tmp->offset_data_cluster = ftell(tmp->file_system);
	fread(tmp->cluster_content, tmp->p_boot_record->cluster_size, tmp->p_boot_record->cluster_count, tmp->file_system);

	// inicializace promennych
	tmp->CLUSTER_CHUNK_SIZE = 20;
	tmp->cluster_chunk_current = 0;
	tmp->cluster_chunks_total = tmp->p_boot_record->cluster_count / tmp->CLUSTER_CHUNK_SIZE;
	tmp->cluster_chunk_read_beginings = malloc(sizeof (int) * tmp->cluster_chunks_total);

	return tmp;
}

int delete_shake_farmer(struct shake_farmer *p_s_f) {
	free(p_s_f->p_boot_record);
	free(p_s_f->fat_item);
	free(p_s_f->p_root_directory);
	free(p_s_f->cluster_content);


	pthread_mutex_destroy(p_s_f->lock_cluster_chunk);
	free(p_s_f->lock_cluster_chunk);

	fclose(p_s_f->file_system);

	free(p_s_f);
	return 1;
}

struct shake_worker *create_shake_worker(struct shake_farmer *p_s_f, int w_id) {
	struct shake_worker* tmp = malloc(sizeof (struct shake_worker));

	tmp->s_f = p_s_f;
	tmp->worker_id = w_id;

	tmp->file_system_operator = fopen(p_s_f->FS_path, "r+");

	return tmp;
}

int delete_shake_worker(struct shake_worker *p_s_w) {
	fclose(p_s_w->file_system_operator);
	free(p_s_w);

	return 1;
}

int shake_next_cluster_chunk(struct shake_worker* p_s_w, struct shake_farmer * p_s_f) {
	if (p_s_f->cluster_chunk_current >= p_s_f->cluster_chunks_total) {
		return 0;
	}
	pthread_mutex_lock(p_s_f->lock_cluster_chunk);
	p_s_w->assigned_cluster_chunk = p_s_f->cluster_chunk_current;
	p_s_f->cluster_chunk_current++;
	pthread_mutex_unlock(p_s_f->lock_cluster_chunk);
	return 1;
}

void *shake_worker_run(struct shake_worker * p_s_w) {

}