#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "fat-operator.h"

// definice na vyznam hodnot FAT tabulky
const int FAT_UNUSED = 65535;
const int FAT_FILE_END = 65534;
const int FAT_BAD_CLUSTER = 65533;

const int FAT_COPIES = 2;
const int RES_CLUSTER_COUNT = 10;

int main_write() {
	int i;

	int alen = 100;
	int blen = 300;
	int clen = 140;
	char a[alen];
	char b[blen];
	char c[blen];
	FILE *fp;
	long rd_count;

	struct boot_record br;
	unsigned int fat[4096 - RES_CLUSTER_COUNT];
	for (int i = 0; i <= 4096 - RES_CLUSTER_COUNT; i++) {
		fat[i] = FAT_UNUSED;
	}

	char cluster_a[128];
	char cluster_b1[128];
	char cluster_b2[128];
	char cluster_b3[128];
	char cluster_c1[128];
	char cluster_c2[128];
	char cluster_empty[128];

	//priprava souboru
	strcpy(cluster_empty, "");
	memset(cluster_a, '\0', sizeof (cluster_a));
	memset(cluster_b1, '\0', sizeof (cluster_b1));
	memset(cluster_b2, '\0', sizeof (cluster_b2));
	memset(cluster_b3, '\0', sizeof (cluster_b3));
	memset(cluster_c1, '\0', sizeof (cluster_c1));
	memset(cluster_c2, '\0', sizeof (cluster_c2));
	strcpy(cluster_a, "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
	strcpy(cluster_b1, "Tohle je fakt dlouhy text, ktery je ulozen v souboru b.txt. Tento soubor zabira opravdu, ale opravdu velike mnozstvi mista, tak");
	strcpy(cluster_b2, "ze v nasi virtualni FATce bude zabirat vice clusteru. Jak je videt v root directory, ma delku 300, takze zabira cele 3 clustery");
	strcpy(cluster_b3, " a to 2,3,4. to je videt jasne z FAT......");
	strcpy(cluster_c1, "A tohle je soubor c.txt - je to uplne obycejny soubor, ve kterem se uplne obycejny text. Jeho zakernosti je, ze neni uplne frag");
	strcpy(cluster_c2, "mentovany...");

	//zapisu natvrdo acko
	fat[0] = FAT_FILE_END;

	struct root_directory root_a;
	memset(root_a.file_name, '\0', sizeof (root_a.file_name));
	strcpy(root_a.file_name, "a.txt");
	root_a.file_size = 100;
	root_a.file_type = 1;
	memset(root_a.file_mod, '\0', sizeof (root_a.file_mod));
	strcpy(root_a.file_mod, "rwxrwxrwx");
	root_a.first_cluster = 0;

	//zapisu natvrdo bcko
	fat[1] = 2;
	fat[2] = 3;
	fat[3] = FAT_FILE_END;
	struct root_directory root_b;
	memset(root_b.file_name, '\0', sizeof (root_b.file_name));
	strcpy(root_b.file_name, "b.txt");
	root_b.file_size = 300;
	root_b.file_type = 1;
	memset(root_b.file_mod, '\0', sizeof (root_b.file_mod));
	strcpy(root_b.file_mod, "rwxrwxrwx");
	root_b.first_cluster = 1;

	//nejake prazdne a vadne clustery pred ceckem
	fat[4] = FAT_UNUSED;
	fat[5] = FAT_BAD_CLUSTER;

	//zapisu natvrdo cecko   
	struct root_directory root_c;
	memset(root_c.file_name, '\0', sizeof (root_c.file_name));
	strcpy(root_c.file_name, "c.txt");
	root_c.file_size = 140;
	root_c.file_type = 1;
	memset(root_c.file_mod, '\0', sizeof (root_c.file_mod));
	strcpy(root_c.file_mod, "rwxrwxrwx");
	root_c.first_cluster = 6;
	fat[6] = 8;
	fat[7] = FAT_UNUSED;
	fat[8] = FAT_FILE_END;

	//zapis dat - boot record
	memset(br.signature, '\0', sizeof (br.signature));
	memset(br.volume_descriptor, '\0', sizeof (br.volume_descriptor));
	strcpy(br.signature, "OK");
	strcpy(br.volume_descriptor, "Testovaci data s trema soubory a.txt, b.txt a c.txt. Cecko NENI fragmentovane");
	br.fat_copies = FAT_COPIES;
	br.fat_type = 12;
	br.cluster_size = 128;
	br.cluster_count = 4096 - RES_CLUSTER_COUNT;
	br.reserved_cluster_count = RES_CLUSTER_COUNT;
	br.root_directory_max_entries_count = 3;

	unlink("output.fat");
	fp = fopen("output.fat", "w");
	//boot record
	fwrite(&br, sizeof (br), 1, fp);
	// FAT copies
	for (i = 0; i < FAT_COPIES; i++) {
		fwrite(&fat, sizeof (fat), 1, fp);
	}
	// root directory
	fwrite(&root_a, sizeof (root_a), 1, fp);
	fwrite(&root_b, sizeof (root_b), 1, fp);
	fwrite(&root_c, sizeof (root_c), 1, fp);
	// clustery - data
	fwrite(&cluster_a, sizeof (cluster_a), 1, fp); //cluster 0
	fwrite(&cluster_b1, sizeof (cluster_b1), 1, fp); //cluster 1
	fwrite(&cluster_b2, sizeof (cluster_b2), 1, fp); //cluster 2
	fwrite(&cluster_b3, sizeof (cluster_b3), 1, fp); //cluster 3
	fwrite(&cluster_empty, sizeof (cluster_empty), 1, fp); //cluster 4
	fwrite(&cluster_empty, sizeof (cluster_empty), 1, fp); //cluster 5
	fwrite(&cluster_c1, sizeof (cluster_c1), 1, fp); //cluster 6
	fwrite(&cluster_empty, sizeof (cluster_empty), 1, fp); //cluster 7
	fwrite(&cluster_c2, sizeof (cluster_c2), 1, fp); //cluster 8
	//vynuluj zbytek datovych bloku
	for (int i = 9; i < 4076; i++)
		fwrite(&cluster_empty, sizeof (cluster_empty), 1, fp);
	fclose(fp);
	return 0;


}// End Of main

FILE *p_file;

/*
Hloupoucka ctecka obsahu dat pro verifikaci 
 */

int main_read() {

	int i;
	//pointery na struktury root a boot                         
	struct boot_record *p_boot_record;
	struct root_directory *p_root_directory;

	//alokujeme pamet
	p_boot_record = (struct boot_record *) malloc(sizeof (struct boot_record));
	p_root_directory = (struct root_directory *) malloc(sizeof (struct root_directory));


	//otevru soubor a pro jistotu skocim na zacatek           
	p_file = fopen("output.fat", "r");
	fseek(p_file, SEEK_SET, 0);

	//prectu boot
	fread(p_boot_record, sizeof (struct boot_record), 1, p_file);
	printf("-------------------------------------------------------- \n");
	printf("BOOT RECORD \n");
	printf("-------------------------------------------------------- \n");
	printf("volume_descriptor :%s\n", p_boot_record->volume_descriptor);
	printf("fat_type :%d\n", p_boot_record->fat_type);
	printf("fat_copies :%d\n", p_boot_record->fat_copies);
	printf("cluster_size :%d\n", p_boot_record->cluster_size);
	printf("root_directory_max_entries_count :%ld\n", p_boot_record->root_directory_max_entries_count);
	printf("cluster count :%d\n", p_boot_record->cluster_count);
	printf("reserved clusters :%d\n", p_boot_record->reserved_cluster_count);
	printf("signature :%s\n", p_boot_record->signature);

	//prectu fat_copies krat 
	printf("-------------------------------------------------------- \n");
	printf("FAT \n");
	printf("-------------------------------------------------------- \n");
	long fat_items = p_boot_record->cluster_count;
	long cl;

	unsigned int *fat_item;
	fat_item = (unsigned int *) malloc(sizeof (unsigned int));
	int fc;
	for (fc = 0; fc < FAT_COPIES; fc++) {
		printf("\nFAT KOPIE %d\n", fc + 1);
		for (cl = 0; cl < fat_items; cl++) {
			fread(fat_item, sizeof (*fat_item), 1, p_file);
			if (*fat_item != FAT_UNUSED) {
				if (*fat_item == FAT_FILE_END)
					printf("%d - FILE_END\n", cl);
				else if (*fat_item == FAT_BAD_CLUSTER)
					printf("%d - BAD_CLUSTER\n", cl);
				else
					printf("%d - %d\n", cl, *fat_item);

			}
		}
	}

	//prectu root tolikrat polik je maximalni pocet zaznamu v bootu - root_directory_max_entries_count        
	printf("-------------------------------------------------------- \n");
	printf("ROOT DIRECTORY \n");
	printf("-------------------------------------------------------- \n");


	for (i = 0; i < p_boot_record->root_directory_max_entries_count; i++) {
		fread(p_root_directory, sizeof (struct root_directory), 1, p_file);
		printf("FILE %d \n", i);
		printf("file_name :%s\n", p_root_directory->file_name);
		printf("file_mod :%s\n", p_root_directory->file_mod);
		printf("file_type :%d\n", p_root_directory->file_type);
		printf("file_size :%d\n", p_root_directory->file_size);
		printf("first_cluster :%d\n", p_root_directory->first_cluster);
	}

	printf("-------------------------------------------------------- \n");
	printf("CLUSTERY - OBSAH \n");
	printf("-------------------------------------------------------- \n");

	char *p_cluster = malloc(sizeof (char) * p_boot_record->cluster_size);
	for (i = 0; i < 10; i++) {
		fread(p_cluster, sizeof (char) * p_boot_record->cluster_size, 1, p_file);
		//pokud je prazdny (tedy zacina 0, tak nevypisuj obsah)
		if (p_cluster[0] != '\0')
			printf("Cluster %d:%s\n", i, p_cluster);
	}

	//uklid
	free(p_cluster);
	free(p_root_directory);
	free(p_boot_record);
	fclose(p_file);

	return 0;


}// End Of main

int main_checkFileLength(int threads) {
	if (threads < 1) {
		return 1;
	}

	printf("Checking lengths");

	return 0;
}

int main_moveClustersToStart(int threads) {
	if (threads < 1) {
		return 1;
	}

	printf("Moving clusters");

	return 0;
}

