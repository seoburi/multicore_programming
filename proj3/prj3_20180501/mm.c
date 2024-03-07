/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your information in the following struct.
 ********************************************************/
team_t team = {
    /* Your student ID */
    "20180501",
    /* Your full name*/
    "kimyeonsu",
    /* Your email address */
    "dustn139@sogang.ac.kr",
};

//#define SUBMIT

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)


#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12) /* Extend heap by this amount (bytes) */
#define SIZE_MAX (1<<30)

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define NEXT_FREE_BLKP(bp) ((char *)(bp))
#define PREV_FREE_BLKP(bp) ((char *)(bp) + WSIZE)

static char *heap_listp;
static char *free_listp;

/* always points to the prologue block, which is an 8-byte allocated block consisting of only a header and a footer */

extern int mm_init (void);
extern void *mm_malloc (size_t size);
extern void mm_free (void *ptr);
extern void *mm_realloc(void *ptr, size_t size);
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *best_fit(size_t asize);
static void place(void *bp, size_t asize);
int mm_check(int verbose);
static void printblock(void *bp);
static void checkblock(void *bp);
static void insert_free(void *bp);
static void delete_free(void *bp);


/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
	heap_listp = NULL;

	if ((heap_listp = mem_sbrk(8 * WSIZE)) == (void *) - 1)
		return -1;
	PUT(heap_listp, 0);								/* Alignment padding */
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));	/* Prologue header */
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));	/* Prologue footer */
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));		/* Epilogue header */
	heap_listp += DSIZE;
	free_listp = heap_listp + DSIZE;
	
	/* Extend the empty heap with a free block of CHUNKISZE byte */
	if (extend_heap(4) == NULL)
		return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char *bp;

    /* Ignore spurious requests */
    if (size <= 0)
	return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
	asize = DSIZE + DSIZE;
    else
	asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);              //가로 안은 할당할 블록의 개수를 뜻함. size에 DSIZE-1을 더하고 DSIZE로 나누면, 최소 size보다 같거나 하나 더 큰 word의 개수가 만들어짐. size가 DSIZE로 나누어떨어지면 상관이 없지만, 그렇지 않으면, word를 하나더 생성해줘야함. double word alignment 만족.

    /* Search the free list for a fit */
    if ((bp = best_fit(asize)) != NULL) {
	place(bp, asize);
	return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
	return NULL;
    place(bp, asize);
    return bp;
}
/* $end mmmalloc */

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);
}

/*
 * mm_realloc - mm_malloc 및 mm_free를 단순히 활용하여 구현
 */
void *mm_realloc(void *ptr, size_t size) {
    size_t old_size;
    size_t newsize;
    
    // 포인터가 NULL인 경우 mm_malloc 호출
    if (ptr == NULL)
        return mm_malloc(size);
    
    // size가 0인 경우 mm_free 호출
    if (!size) {
        mm_free(ptr);
        return NULL;
    }
    
    // 이전 크기와 새로운 크기 조정
    old_size = GET_SIZE(HDRP(ptr));
    
    // 오버헤드와 정렬 요구 사항을 포함한 블록 크기 조정
    if (size <= DSIZE)
        newsize = DSIZE + DSIZE;
    else
        newsize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    
    // 새로운 크기가 이전 크기보다 작은 경우 기존 포인터 반환
    if (newsize <= old_size)
        return ptr;
    
    void *newp = NULL;
    void *next = NEXT_BLKP(ptr);
    size_t next_size = GET_SIZE(HDRP(next));
    
    // 인접한 빈 블록 사용 가능한 경우 크기 조절하고 기존 포인터 반환
    if (!GET_ALLOC(HDRP(next)) && (old_size + next_size >= newsize)) {
        delete_free(next);
        PUT(HDRP(ptr), PACK(old_size + next_size, 1));
        PUT(FTRP(ptr), PACK(old_size + next_size, 1));
        newp = ptr;
    } else {
        // 새로운 크기로 mm_malloc 호출하여 메모리 블록 할당
        if ((newp = mm_malloc(newsize)) == NULL) {
            printf("ERROR: mm_malloc failed in mm_realloc\n");
            exit(1);
        }
        place(newp, newsize);
        
        // 이전 데이터 복사 후 기존 포인터 해제
        memcpy(newp, ptr, old_size - SIZE_T_SIZE);
        mm_free(ptr);
    }
    
    return newp;
}


/*
 * best_fit - 요청된 크기에 적합한 빈 블록을 빈 블록 목록에서 찾음
 */
static void* best_fit(size_t asize) {
    void *bp;
    void *best;
    size_t flag = 0;
    size_t min_size = SIZE_MAX;
    
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = GET(NEXT_FREE_BLKP(bp))) {
        // 요청된 크기를 수용 가능하면서 가장 작은 블록을 선택
        if ((asize <= GET_SIZE(HDRP(bp))) && (GET_SIZE(HDRP(bp)) < min_size)) {
            min_size = GET_SIZE(HDRP(bp));
            flag = 1;
            best = bp;
        }
    }
    if (flag)
        return best;
    
    return NULL;
}


static void insert_free(void *bp) {
	// 'bp'의 이전 빈 블록 포인터를 NULL로 설정
	PUT(PREV_FREE_BLKP(bp), NULL);
	// 현재 빈 블록 리스트의 첫 번째 블록의 이전 빈 블록 포인터를 'bp'로 설정
	PUT(PREV_FREE_BLKP(free_listp), bp);

	// 'bp'의 다음 빈 블록 포인터를 현재 빈 블록 리스트의 첫 번째 블록으로 설정
	PUT(NEXT_FREE_BLKP(bp), free_listp);

	// 'bp'를 새로운 빈 블록 리스트의 첫 번째 블록으로 설정
	free_listp = bp;
}

static void delete_free(void *bp) {
	// 'bp'의 이전 빈 블록 포인터를 가져옴
	char *prev = GET(PREV_FREE_BLKP(bp));
	// 'bp'의 다음 빈 블록 포인터를 가져옴
	char *next = GET(NEXT_FREE_BLKP(bp));

	// 이전 빈 블록 포인터가 NULL인 경우
	if (!prev) {
		// 'bp'를 새로운 빈 블록 리스트의 첫 번째 블록으로 설정
		free_listp = next;
		
		// 다음 빈 블록의 이전 빈 블록 포인터를 NULL로 설정
		PUT(PREV_FREE_BLKP(next), NULL);
	}
	else {
		// 이전 빈 블록의 다음 빈 블록 포인터를 'next'로 설정
		PUT(NEXT_FREE_BLKP(prev), next);
		
		// 다음 빈 블록의 이전 빈 블록 포인터를 'prev'로 설정
		PUT(PREV_FREE_BLKP(next), prev);
	}
return;
}

/*
 * coalesce - 경계 태그 결합. 결합된 블록의 포인터 반환
 */
static void *coalesce(void *bp)
{
    // 이전 블록이 할당되었는지 확인하고, 첫 번째 블록을 가리키는 경우 처리
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
    // 다음 블록이 할당되었는지 확인
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    // 현재 블록의 크기를 가져옴
    size_t size = GET_SIZE(HDRP(bp));

    // 이전 블록과 다음 블록 모두 할당된 경우 (Case 1)
    if (prev_alloc && next_alloc) {
        insert_free(bp);
        return bp;
    }
    // 이전 블록은 할당되고 다음 블록이 비어있는 경우 (Case 2)
    else if (prev_alloc && !next_alloc) {
        // 다음 블록을 빈 블록 리스트에서 제거
        delete_free(NEXT_BLKP(bp));
        // 현재 블록과 다음 블록의 크기를 결합
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        // 헤더와 푸터에 결합된 크기와 할당되지 않음을 표시
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    // 이전 블록이 비어있고 다음 블록이 할당된 경우 (Case 3)
    else if (!prev_alloc && next_alloc) {
        // 이전 블록을 빈 블록 리스트에서 제거
        delete_free(PREV_BLKP(bp));
        // 현재 블록과 이전 블록의 크기를 결합
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        // 이전 블록의 헤더와 현재 블록의 푸터에 결합된 크기와 할당되지 않음을 표시
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        // 포인터를 이전 블록으로 옮긴다.
        bp = PREV_BLKP(bp);
    }
    // 이전 블록과 다음 블록이 둘 다 비어있는 경우 (case 4)
    else {
        // 다음 블록과 이전 블록을 빈 블록 리스트에서 제거
        delete_free(NEXT_BLKP(bp));
        delete_free(PREV_BLKP(bp));
        // 현재 블록과 이전 블록, 다음 블록의 크기를 결합
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        // 이전 블록의 헤더와 다음 블록의 푸터에 결합된 크기와 할당되지 않음을 표시
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        // 포인터를 이전 블록으로 옮긴다.
        bp = PREV_BLKP(bp);
    }
    // 결합된 블록을 빈 블록 리스트에 삽입
    insert_free(bp);

    return bp;
}



static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1)
	return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* new epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

static void place(void *bp, size_t asize)
{
	// csize는 현재 블록의 크기
	size_t csize = GET_SIZE(HDRP(bp));
	// 현재 블록을 빈 블록 리스트에서 제거
	delete_free(bp);

	// 현재 블록의 크기에서 할당을 위해 필요한 크기(asize)를 뺀 값이 최소 블록 크기보다 큰 경우
	if ((csize - asize) >= (DSIZE + DSIZE)) {
		// 현재 블록의 헤더와 푸터를 asize 크기로 설정하고 할당된 상태로 표시
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		
		// 다음 블록으로 이동
		bp = NEXT_BLKP(bp);
		
		// 남은 블록의 헤더와 푸터를 csize-asize 크기로 설정하고 빈 상태로 표시
		PUT(HDRP(bp), PACK(csize-asize, 0));
		PUT(FTRP(bp), PACK(csize-asize, 0));
		
		// 남은 블록을 빈 블록 리스트에 삽입
		insert_free(bp);
	}
	else {
		// 현재 블록의 헤더와 푸터를 csize 크기로 설정하고 할당된 상태로 표시
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
}

static void checkblock(void *bp)
{
	if ((unsigned int )bp % DSIZE)
		printf("Error: %p is not doubleword aligned\n", bp);
	if (GET_SIZE(HDRP(bp)) != GET_SIZE(FTRP(bp)) || GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp)))
		printf("Error: header does not match footer\n");
}

/*
 * mm_check - Check the heap for consistency
 */
int mm_check(int verbose)
{
    char *bp = heap_listp;

    if (verbose)
	printf("Heap (%p):\n", heap_listp);

    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp)))
	printf("Bad prologue header\n");
    checkblock(heap_listp);

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
	if (verbose)
	    printblock(bp);
	checkblock(bp);
    }

    if (verbose)
	printblock(bp);
    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
	printf("Bad epilogue header\n");
    return 1;
}

static void printblock(void *bp)
{
    size_t hsize, halloc, fsize, falloc;

    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp));
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));

    if (hsize == 0) {
	printf("%p: EOL\n", bp);
	return;
    }

    printf("%p: header: [%ld:%c] footer: [%ld:%c]\n", bp,
	   hsize, (halloc ? 'a' : 'f'),
	   fsize, (falloc ? 'a' : 'f'));
}