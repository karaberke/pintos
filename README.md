# PintOS course project

Our CS 439 PintOS work progressed through thread scheduling, user programs, virtual memory, and file-system assignments. This is **one project with four submitted snapshots**. Each milestone is preserved as a tag in this repository:

| Milestone | Source snapshot |
|---|---|
| Thread scheduling and synchronization, including priority donation | [`project-1-threads`](https://github.com/karaberke/pintos/tree/project-1-threads) |
| User programs and system calls | [`project-2-user-programs`](https://github.com/karaberke/pintos/tree/project-2-user-programs) |
| Demand-paged virtual memory, frame eviction, swap, lazy loading, and stack growth | [`project-3-virtual-memory`](https://github.com/karaberke/pintos/tree/project-3-virtual-memory) |
| Extensible files with multilevel indexed inodes and a 64-sector write-behind buffer cache | [`project-4-file-system`](https://github.com/karaberke/pintos/tree/project-4-file-system) |

The default `main` branch is the file-system submission snapshot. **It does not contain every earlier implementation:** the virtual-memory source and some thread priority-donation code are present in their milestone tags, but absent from the file-system snapshot. The tags preserve the actual submitted states; they are not a claim that all four stages were integrated into one tested build.

## Source and attribution

The base PintOS code is credited to Stanford University and retains the upstream `LICENSE` file in each snapshot. This is group coursework modifying that base, not an operating system authored from scratch. Student identifiers in course README files and planning logs were excluded from the public snapshots. Contributor names in source comments remain where they document pair work.

## Build and testing

The archived Makefiles and tests are included where available. These snapshots have not been rebuilt or rerun in the current Windows environment. Check the Makefile and course-specific dependencies for the tag you want to build. No performance or full-suite passing claim is made here.
