#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#include "cluster-shake.h"

#ifndef FAT_UNUSED
#define FAT_UNUSED 65535
#define FAT_FILE_END 65534
#define FAT_BAD_CLUSTER 65533
#endif

const int CL_CHUNK_SIZE = 4;
const int NO_SHAKE_JOB = -1;

int shake_analyze_root_directory(struct shake_farmer *p_s_f) {
	unsigned int i;
	struct root_directory *cur_rd;
	for (i = 0; i < p_s_f->p_boot_record->cluster_count; i++) {
		p_s_f->rd_links[i] = FAT_UNUSED;
	}
	for (i = 0; i < p_s_f->p_boot_record->root_directory_max_entries_count; i++) {
		cur_rd = p_s_f->p_root_directory + i;
		if (cur_rd->first_cluster > p_s_f->p_boot_record->cluster_count) {
#ifdef DEBUG
			printf("Errorneous first cluster on:\n");
#endif
			read_print_root_directory(p_s_f->p_root_directory + i, i);
			continue;
		}
		p_s_f->rd_links[cur_rd->first_cluster] = i;
		p_s_f->FAT_rev[cur_rd->first_cluster] = FAT_FILE_END;
	}
#ifdef DEBUG
	printf("SA_RD done\n");
#endif
}

int shake_analyze_fat(struct shake_farmer *p_s_f) {
	int chunk_last_index = p_s_f->CLUSTER_CHUNK_SIZE - 1;

	int i, cl_ch_i, non_empty, bad_clusters, mod, cluster_last_non_empty;
	cl_ch_i = non_empty = bad_clusters = 0;

	int cur_FAT_item;


	for (i = 0; i < p_s_f->p_boot_record->cluster_count; i++) {
		cur_FAT_item = p_s_f->FAT[i];
		if (cur_FAT_item == FAT_UNUSED) {
			continue;
		}
		if (cur_FAT_item == FAT_BAD_CLUSTER) {
			bad_clusters++;
			continue;
		}
		cluster_last_non_empty = i;

		mod = non_empty++ % p_s_f->CLUSTER_CHUNK_SIZE;
		if (mod == 0) {
			p_s_f->cluster_chunk_read_beginings[cl_ch_i] = cluster_last_non_empty;
		} else if (mod == chunk_last_index) {
			p_s_f->cluster_chunk_read_ends[cl_ch_i++] = cluster_last_non_empty;
		}
		if (cur_FAT_item != FAT_FILE_END) {
			p_s_f->FAT_rev[cur_FAT_item] = cluster_last_non_empty;
		}
	}

	mod = non_empty % p_s_f->CLUSTER_CHUNK_SIZE;
	p_s_f->cluster_chunks_not_empty = (non_empty / p_s_f->CLUSTER_CHUNK_SIZE);
	if (mod) {
		p_s_f->cluster_chunks_not_empty++;
		p_s_f->cluster_chunk_last_size = mod;
		p_s_f->cluster_chunk_read_ends[cl_ch_i] = cluster_last_non_empty;
	} else {
		p_s_f->cluster_chunk_last_size = p_s_f->CLUSTER_CHUNK_SIZE;
	}


#ifdef DEBUG
	printf("done\n");
	printf("Non empty clusters: %04d, bad clusters: %02d\n", non_empty, bad_clusters);
	printf("Last chunk: %02d\tLast chunk size: %d\n\n",
			p_s_f->cluster_chunks_not_empty, p_s_f->cluster_chunk_last_size);
#endif
	return 0;

}

int write_shaken_headder(struct shake_farmer *p_s_f, char *file_name) {
	FILE* p_file;
	int i;

	unlink(file_name);
	p_file = fopen(file_name, "w");
	fwrite(p_s_f->p_boot_record, sizeof (struct boot_record), 1, p_file);
	for (i = 0; i < p_s_f->p_boot_record->fat_copies; i++) {
		fwrite(p_s_f->FAT, sizeof (unsigned int), p_s_f->p_boot_record->cluster_count, p_file);
	}
	fwrite(p_s_f->p_root_directory, sizeof (struct root_directory), p_s_f->p_boot_record->root_directory_max_entries_count, p_file);
	fwrite(p_s_f->cluster_content, sizeof (char) * p_s_f->p_boot_record->cluster_size, p_s_f->p_boot_record->cluster_count, p_file);
}

struct shake_farmer *create_shake_farmer(char* FS_path) {
	struct shake_farmer* tmp = malloc(sizeof (struct shake_farmer));
	FILE* p_file;
	int i;

	// otevru soubor a pro jistotu skocim na zacatek           
	p_file = fopen(FS_path, "r");
	fseek(p_file, 0, SEEK_SET);

	tmp->FS_path = FS_path;
	tmp->file_system = p_file;

	// inicializace boot record
	tmp->p_boot_record = malloc(sizeof (struct boot_record));
	fread(tmp->p_boot_record, sizeof (struct boot_record), 1, tmp->file_system);

	// inicializace a nacteni FAT
	tmp->FAT = malloc(tmp->p_boot_record->cluster_count * sizeof (unsigned int));
	tmp->FAT_rev = malloc(tmp->p_boot_record->cluster_count * sizeof (unsigned int));
	tmp->offset_fat = ftell(tmp->file_system);
	for (i = 0; i < tmp->p_boot_record->fat_copies; i++) {
		fread(tmp->FAT, sizeof (unsigned int), tmp->p_boot_record->cluster_count, tmp->file_system);
	}

	// inicializace cluster mutexu
	tmp->lock_cluster_chunk_counter = malloc(sizeof (pthread_mutex_t));
	pthread_mutex_init(tmp->lock_cluster_chunk_counter, NULL);

	tmp->sem_cluster_access = malloc(sizeof (sem_t) * tmp->p_boot_record->cluster_count);
	for (i = 0; i < tmp->p_boot_record->cluster_count; i++) {
		sem_init(tmp->sem_cluster_access + i, 0, 2);
	}
	// inicializace a nacteni root directory + znacek zpetne ukazujicich z FAT na root_directory
	tmp->offset_root_directory = ftell(tmp->file_system);
	tmp->p_root_directory = malloc(sizeof (struct root_directory) * tmp->p_boot_record->root_directory_max_entries_count);
	fread(tmp->p_root_directory, sizeof (struct root_directory), tmp->p_boot_record->root_directory_max_entries_count, tmp->file_system);

	tmp->rd_links = malloc(sizeof (unsigned int) * tmp->p_boot_record->cluster_count);

	// inicializace a nacteni datovych clusteru
	tmp->offset_data_cluster = ftell(tmp->file_system);
	tmp->cluster_content = malloc(sizeof (char) * tmp->p_boot_record->cluster_size * tmp->p_boot_record->cluster_count);
	fread(tmp->cluster_content, sizeof (char) * tmp->p_boot_record->cluster_size, tmp->p_boot_record->cluster_count, tmp->file_system);

	// inicializace promennych
	*(int *) &tmp->CLUSTER_CHUNK_SIZE = CL_CHUNK_SIZE;
	*(char *) &tmp->empty_char = '\0';

	tmp->cluster_chunk_read_beginings = malloc(sizeof (int) * (tmp->p_boot_record->cluster_count / tmp->CLUSTER_CHUNK_SIZE));
	tmp->cluster_chunk_read_ends = malloc(sizeof (int) * (tmp->p_boot_record->cluster_count / tmp->CLUSTER_CHUNK_SIZE));
	tmp->cluster_chunk_current = 0;


	write_shaken_headder(tmp, "shaken.fat");

	return tmp;
}

int delete_shake_farmer(struct shake_farmer *p_s_f) {
	int i;

	free(p_s_f->p_boot_record);
	free(p_s_f->FAT);
	free(p_s_f->FAT_rev);
	free(p_s_f->p_root_directory);
	free(p_s_f->rd_links);

	free(p_s_f->cluster_content);

	free(p_s_f->cluster_chunk_read_beginings);
	free(p_s_f->cluster_chunk_read_ends);


	pthread_mutex_destroy(p_s_f->lock_cluster_chunk_counter);
	free(p_s_f->lock_cluster_chunk_counter);

	for (i = 0; i < p_s_f->p_boot_record->cluster_count; i++) {
		sem_destroy(p_s_f->sem_cluster_access + i);
	}
	free(p_s_f->sem_cluster_access);

	fclose(p_s_f->file_system);

	free(p_s_f);
	return 1;
}

struct shake_worker *create_shake_worker(struct shake_farmer *p_s_f, int w_id) {
	struct shake_worker* tmp = malloc(sizeof (struct shake_worker));

	tmp->s_f = p_s_f;
	tmp->worker_id = w_id;
	tmp->nonfree_naps = 0;

	tmp->file_system_operator = fopen("shaken.fat", "r+");

	tmp->hold_cluster = malloc(sizeof (char) * p_s_f->p_boot_record->cluster_size);
	//tmp->file_system_operator = fopen(p_s_f->FS_path, "r+");

	return tmp;
}

int delete_shake_worker(struct shake_worker *p_s_w) {
	fclose(p_s_w->file_system_operator);
	free(p_s_w->hold_cluster);
	free(p_s_w);

	return 1;
}

int shake_next_cluster_chunk(struct shake_worker* p_s_w, struct shake_farmer * p_s_f) {
	if (p_s_f->cluster_chunk_current >= p_s_f->cluster_chunks_not_empty) {
		return 0;
	}
	pthread_mutex_lock(p_s_f->lock_cluster_chunk_counter);
	p_s_w->assigned_cluster_chunk = p_s_f->cluster_chunk_current;
	p_s_f->cluster_chunk_current++;
	pthread_mutex_unlock(p_s_f->lock_cluster_chunk_counter);
	return 1;
}

int shake_worker_search_fat(struct shake_worker *p_s_w, struct shake_farmer *p_s_f) {
	p_s_w->search_item = p_s_f->FAT[p_s_w->search_chunk_start + ++(p_s_w->search_index)];
	while (p_s_w->search_item == FAT_BAD_CLUSTER || p_s_w->search_item == FAT_UNUSED) { // hledej neprázdné clustery
		// overeni ze se prohledava spravny cluster chunk
		if (p_s_w->search_index > p_s_w->search_chunk_end) {
			return 0;
		}
		// dalsi polozka FAT
		p_s_w->search_item = p_s_f->FAT[p_s_w->search_chunk_start + ++(p_s_w->search_index)];
	}
	return 1;
}

int valid_cluster_link(unsigned int p) {
	return !(p == FAT_UNUSED || p == FAT_FILE_END || p == FAT_BAD_CLUSTER);
}

int shake_worker_move_cluster(struct shake_farmer *p_s_f, struct shake_worker *p_s_w, unsigned int where_to, unsigned int where_from) {
	if (where_to == where_from) {
		return 0;
	}
	unsigned int previous_in_chain, next_in_chain, file_num;
	// double-locks current cluster to make sure no other operation is currently happening to actual chunk
	sem_wait(p_s_f->sem_cluster_access + where_from);
	sem_wait(p_s_f->sem_cluster_access + where_from);

	// sleep as long as destination cluster isn't free, then double-lock it
	while (p_s_f->FAT[where_to] != FAT_UNUSED) {
		if (p_s_f->FAT[where_to] == FAT_BAD_CLUSTER) {
			break;
		}
		p_s_w->nonfree_naps++;
		sleep(1);
	}
	sem_wait(p_s_f->sem_cluster_access + where_to);
	sem_wait(p_s_f->sem_cluster_access + where_to);

	// lock adjacent clusters
	previous_in_chain = p_s_f->FAT_rev[where_from];
	if (valid_cluster_link(previous_in_chain)) {
		sem_wait(p_s_f->sem_cluster_access + previous_in_chain);
	} else {
		previous_in_chain = FAT_FILE_END;
	}
	next_in_chain = p_s_f->FAT[where_from];
	if (valid_cluster_link(next_in_chain)) {
		sem_wait(p_s_f->sem_cluster_access + next_in_chain);
	} else {
		next_in_chain = FAT_FILE_END;
	}

	// load'n'store current cluster from source to its destination
	fseek(p_s_w->file_system_operator, p_s_f->offset_data_cluster + where_from * sizeof (char) * p_s_f->p_boot_record->cluster_size, SEEK_SET);
	fread(p_s_w->hold_cluster, sizeof (char) * p_s_f->p_boot_record->cluster_size, 1, p_s_w->file_system_operator);
	fseek(p_s_w->file_system_operator, p_s_f->offset_data_cluster + where_to * sizeof (char) * p_s_f->p_boot_record->cluster_size, SEEK_SET);
	fwrite(p_s_w->hold_cluster, sizeof (char) * p_s_f->p_boot_record->cluster_size, 1, p_s_w->file_system_operator);
	// make cluster appeal like it's empty to fat-reader
	fseek(p_s_w->file_system_operator, p_s_f->offset_data_cluster + where_from * sizeof (char) * p_s_f->p_boot_record->cluster_size, SEEK_SET);
	fwrite(&(p_s_f->empty_char), sizeof (char), 1, p_s_w->file_system_operator);

	// if currently moved cluster is beginning of a file, update corresponding root directory entry
	file_num = p_s_f->rd_links[where_from];

	// update FAT nd FAT_rev to represent the cluster state after the move
	p_s_f->FAT[where_to] = p_s_f->FAT[where_from];
	p_s_f->FAT[where_from] = FAT_UNUSED;
	if (previous_in_chain != FAT_FILE_END) {
		p_s_f->FAT[previous_in_chain] = where_to;
	}
	if (previous_in_chain != FAT_FILE_END) {
		p_s_f->FAT_rev[next_in_chain] = where_to;
	}

	// release of semaphore locks
	if (previous_in_chain != FAT_FILE_END) {
		sem_post(p_s_f->sem_cluster_access + previous_in_chain);
	}
	if (next_in_chain != FAT_FILE_END) {
		sem_post(p_s_f->sem_cluster_access + next_in_chain);
	}
	sem_post(p_s_f->sem_cluster_access + where_from);
	sem_post(p_s_f->sem_cluster_access + where_from);
	sem_post(p_s_f->sem_cluster_access + where_to);
	sem_post(p_s_f->sem_cluster_access + where_to);

#ifdef DEBUG
	printf("(W%02d-CH%02d): Put %03d[%04d+%d]: %05d\n"
			"Content: %s\n",
			p_s_w->worker_id, p_s_w->assigned_cluster_chunk,
			where_to, p_s_w->search_chunk_start, p_s_w->search_index, p_s_w->search_item,
			p_s_w->hold_cluster);
#endif
	return 1;

}

void *shake_worker_run(struct shake_worker * p_s_w) {
	struct shake_farmer *p_s_f = p_s_w->s_f;
	int put_index;
#ifdef DEBUG
	printf("Running shake worker %02d with chunk size %d\n", p_s_w->worker_id, p_s_f->CLUSTER_CHUNK_SIZE);
#endif
	while (shake_next_cluster_chunk(p_s_w, p_s_w->s_f)) { // Dokud jsou nepřesunuté chunky
		p_s_w->chunk_put_offset = p_s_w->assigned_cluster_chunk * p_s_f->CLUSTER_CHUNK_SIZE;
		p_s_w->search_chunk_start = p_s_f->cluster_chunk_read_beginings[p_s_w->assigned_cluster_chunk];
		p_s_w->search_chunk_end = p_s_f->cluster_chunk_read_ends[p_s_w->assigned_cluster_chunk];

		/*printf("(W%02d-CH%02d) got chunk: <%04d, %04d>\n",
				p_s_w->worker_id, p_s_w->assigned_cluster_chunk,
				p_s_w->search_chunk_start, p_s_w->search_chunk_end);*/

		p_s_w->search_index = -1;
		for (put_index = 0; put_index < p_s_f->CLUSTER_CHUNK_SIZE; put_index++) { // přesouvej nalezené clustery
			if (p_s_w->assigned_cluster_chunk == p_s_f->cluster_chunks_not_empty - 1 && put_index >= p_s_f->cluster_chunk_last_size) {
				break;
			}

			// prohledani FAT podle cluster chunku
			if (!shake_worker_search_fat(p_s_w, p_s_f)) {
#ifdef DEBUG
				printf("(W%02d-CH%02d): SI = %04d exceeded SCE = %04d\n",
						p_s_w->worker_id, p_s_w->assigned_cluster_chunk,
						p_s_w->search_index, p_s_w->search_chunk_end);
#endif
				break;
			}
			shake_worker_move_cluster(p_s_f, p_s_w, p_s_w->chunk_put_offset + put_index, p_s_w->search_chunk_start + p_s_w->search_index);
		}
#ifdef DEBUG
		printf("(W%02d-CH%02d): Chunk done\n", p_s_w->worker_id, p_s_w->assigned_cluster_chunk);
#endif
	}
}

void shake_write_back(struct shake_farmer *p_s_f) {
	int fat_size;
	fat_size = sizeof (unsigned int) * p_s_f->p_boot_record->cluster_count;
	fseek(p_s_f->file_system, p_s_f->offset_fat, SEEK_SET);
	fwrite(p_s_f->FAT, fat_size, p_s_f->p_boot_record->fat_copies, p_s_f->file_system);

	fseek(p_s_f->file_system, p_s_f->offset_root_directory, SEEK_SET);
	fwrite(p_s_f->p_root_directory, sizeof (struct root_directory), p_s_f->p_boot_record->root_directory_max_entries_count, p_s_f->file_system);
}
