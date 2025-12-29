## Benchmark Configuration

- Keys: 10000 random 64-bit integers (as 8-byte big-endian strings)
- Duration: 500ms per test
- All containers protected with std::shared_mutex for fair comparison

### FIND (contains)

| Threads | tktrie | std::map | std::unordered_map | tktrie/map | tktrie/umap |
|---------|--------|----------|-------------------|------------|-------------|
| 1 | 21.1M | 45.7M | 48.4M | **0.5x** | **0.4x** |
| 2 | 43.5M | 10.8M | 14.1M | **4.0x** | **3.1x** |
| 4 | 86.1M | 9.5M | 9.3M | **9.1x** | **9.3x** |
| 8 | 93.9M | 4.7M | 6.8M | **19.8x** | **13.9x** |
| 12 | 93.3M | 3.3M | 4.0M | **27.9x** | **23.2x** |
| 16 | 96.3M | 3.8M | 3.6M | **25.5x** | **27.1x** |

### INSERT

| Threads | tktrie | std::map | std::unordered_map | tktrie/map | tktrie/umap |
|---------|--------|----------|-------------------|------------|-------------|
| 1 | 6.7M | 4.5M | 10.0M | 1.5x | 0.7x |
| 2 | 2.5M | 1.4M | 3.3M | 1.8x | 0.8x |
| 4 | 2.0M | 1.3M | 2.8M | 1.6x | 0.7x |
| 8 | 1.9M | 1.2M | 3.0M | 1.7x | 0.6x |
| 12 | 1.1M | 1.0M | 2.6M | 1.1x | 0.4x |
| 16 | 1.6M | 1.0M | 2.4M | 1.7x | 0.7x |

### ERASE

| Threads | tktrie | std::map | std::unordered_map | tktrie/map | tktrie/umap |
|---------|--------|----------|-------------------|------------|-------------|
| 1 | 27.2M | 35.5M | 35.7M | 0.8x | 0.8x |
| 2 | 7.1M | 11.4M | 11.6M | 0.6x | 0.6x |
| 4 | 5.9M | 9.9M | 10.7M | 0.6x | 0.5x |
| 8 | 4.8M | 6.3M | 7.8M | 0.8x | 0.6x |
| 12 | 4.2M | 5.6M | 6.4M | 0.8x | 0.7x |
| 16 | 4.6M | 6.1M | 8.8M | 0.8x | 0.5x |

### FIND with Concurrent Writers

| Readers | Writers | tktrie | std::map | std::unordered_map | tktrie/map | tktrie/umap |
|---------|---------|--------|----------|-------------------|------------|-------------|
| 4 | 0 | 88.1M | 7.3M | 9.2M | **12.0x** | **9.5x** |
| 4 | 1 | 87.1M | 10.2M | 9.7M | **8.6x** | **9.0x** |
| 4 | 2 | 62.6M | 7.5M | 7.9M | **8.3x** | **8.0x** |
| 4 | 4 | 47.0M | 8.4M | 7.9M | **5.6x** | **5.9x** |
| 8 | 0 | 96.0M | 6.1M | 6.2M | **15.7x** | **15.4x** |
| 8 | 2 | 80.6M | 5.3M | 4.9M | **15.2x** | **16.5x** |
| 8 | 4 | 69.7M | 4.5M | 4.7M | **15.6x** | **14.7x** |
| 12 | 0 | 91.7M | 4.3M | 4.3M | **21.2x** | **21.1x** |
| 12 | 4 | 75.4M | 4.8M | 4.7M | **15.8x** | **15.9x** |
