#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>

typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
} CPUStats;

int get_cpu_count() {
    FILE *file = fopen("/proc/cpuinfo", "r");
    if (!file) {
        perror("Error opening /proc/cpuinfo");
        return -1;
    }

    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "processor", 9) == 0) {
            count++;
        }
    }

    fclose(file);
    return count;
}

int get_cpu_stats(CPUStats *stats, int cpu_count) {
    FILE *file = fopen("/proc/stat", "r");
    if (!file) {
        perror("Error opening /proc/stat");
        return 1;
    }

    char line[256];
    int cpu_idx = 0;
    while (fgets(line, sizeof(line), file) && cpu_idx <= cpu_count) {
        if (strncmp(line, "cpu", 3) == 0) {
            sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu", &stats[cpu_idx].user, &stats[cpu_idx].nice, &stats[cpu_idx].system,
                   &stats[cpu_idx].idle, &stats[cpu_idx].iowait, &stats[cpu_idx].irq, &stats[cpu_idx].softirq);
            cpu_idx++;
        }
    }

    fclose(file);
    return 0;
}

double calculate_cpu_load_percentage(const CPUStats *prev, const CPUStats *curr) {
    unsigned long long prev_total_time = prev->user + prev->nice + prev->system + prev->idle + prev->iowait + prev->irq + prev->softirq;
    unsigned long long curr_total_time = curr->user + curr->nice + curr->system + curr->idle + curr->iowait + curr->irq + curr->softirq;

    unsigned long long total_time_diff = curr_total_time - prev_total_time;
    unsigned long long idle_time_diff = curr->idle - prev->idle;

    return ((double)(total_time_diff - idle_time_diff) / total_time_diff) * 100;
}

int get_disk_space(unsigned long long *total_space, unsigned long long *free_space) {
    struct statvfs stat;

    if (statvfs("/", &stat) != 0) {
        perror("Error getting disk space");
        return 1;
    }

    *total_space = (unsigned long long)stat.f_frsize * stat.f_blocks;
    *free_space = (unsigned long long)stat.f_frsize * stat.f_bfree;

    return 0;
}

int get_network_speed(const char *iface, unsigned long long *rx_speed, unsigned long long *tx_speed) {
    FILE *file = fopen("/proc/net/dev", "r");
    if (!file) {
        perror("Error opening /proc/net/dev");
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char if_name[16];
                unsigned long long rx_bytes, tx_bytes;
        if (sscanf(line, "%15[^:]: %*u %llu %*u %*u %*u %*u %*u %*u %*u %llu", if_name, &rx_bytes, &tx_bytes) == 3) {
            if (strcmp(if_name, iface) == 0) {
                *rx_speed = rx_bytes;
                *tx_speed = tx_bytes;
                break;
            }
        }
    }

    fclose(file);
    return 0;
}

char *get_first_network_iface() {
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        perror("Error getting network interfaces");
        return NULL;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            char *iface = strdup(ifa->ifa_name);
            freeifaddrs(ifaddr);
            return iface;
        }
    }

    freeifaddrs(ifaddr);
    return NULL;
}

int main() {
    int cpu_count = get_cpu_count();
    if (cpu_count < 1) {
        fprintf(stderr, "Error getting CPU count\n");
        return 1;
    }

    CPUStats *prev_stats = malloc((cpu_count + 1) * sizeof(CPUStats));
    CPUStats *curr_stats = malloc((cpu_count + 1) * sizeof(CPUStats));

    get_cpu_stats(prev_stats, cpu_count);

    char *iface = get_first_network_iface();
    if (!iface) {
        fprintf(stderr, "No network interface found\n");
        return 1;
    }

    unsigned long long prev_rx_speed = 0, prev_tx_speed = 0;
    get_network_speed(iface, &prev_rx_speed, &prev_tx_speed);
    sleep(1);

    while (1) {
        get_cpu_stats(curr_stats, cpu_count);

        printf("Total CPU Load: %.2f%%\n", calculate_cpu_load_percentage(&prev_stats[0], &curr_stats[0]));
        for (int i = 1; i <= cpu_count; i++) {
            printf("CPU %d Load: %.2f%%\n", i - 1, calculate_cpu_load_percentage(&prev_stats[i], &curr_stats[i]));
        }

        unsigned long long total_space, free_space;
        if (get_disk_space(&total_space, &free_space) == 0) {
            printf("Total Disk Space: %.2f GB | Free Disk Space: %.2f GB\n", (double)total_space / (1024 * 1024 * 1024), (double)free_space / (1024 * 1024 * 1024));
        }

        unsigned long long curr_rx_speed, curr_tx_speed;
        if (get_network_speed(iface, &curr_rx_speed, &curr_tx_speed) == 0) {
            unsigned long long rx_diff = curr_rx_speed - prev_rx_speed;
            unsigned long long tx_diff = curr_tx_speed - prev_tx_speed;
            printf("Network Interface: %s | RX Speed: %.2f Mb/s | TX Speed: %.2f Mb/s\n", iface, (double)rx_diff / (1024 * 1024 / 8), (double)tx_diff / (1024 * 1024 / 9));
            prev_rx_speed = curr_rx_speed;
            prev_tx_speed = curr_tx_speed;
        }

        printf("\n");
        memcpy(prev_stats, curr_stats, (cpu_count + 1) * sizeof(CPUStats));
        sleep(1);
    }

    free(prev_stats);
    free(curr_stats);
    free(iface);
    return 0;
}

