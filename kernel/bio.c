// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKET  5
#define HASH(blockno) (blockno%NBUCKET)

extern uint ticks;
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  int count_used;
  struct buf hashbuckets[NBUCKET];
  struct spinlock hashlocks[NBUCKET];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i=0;i<NBUCKET;i++)
  {
    bcache.hashbuckets[i].next=0;
    initlock(&(bcache.hashlocks[i]),"bcache.bucket");
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++)
  {
    initsleeplock(&b->lock, "buffer");
  }
  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  //printf("okbget\n");
  struct buf *b;

  int index=HASH(blockno);

  acquire(&bcache.hashlocks[index]);
  //找到了对应的buf
  b=bcache.hashbuckets[index].next;
  
  for(;b!=0;b=b->next)
  {
    //printf("findbuf\n");
    if(b->dev==dev && b->blockno==blockno)
    {
      b->refcnt++;
      release(&bcache.hashlocks[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  //没找到blockno，要找一个空闲的buf
  acquire(&bcache.lock);
  struct buf *leastrecent=0;
  for(b=bcache.buf;b<bcache.buf+NBUF;b++)
  {
    if(b->refcnt==0)
    {
      if(leastrecent==0)leastrecent=b;
      else if(leastrecent->tick > b->tick)leastrecent=b;
    }
  }

  if(leastrecent==0)panic("bget: no buffers");

  uint oldtick=leastrecent->tick;
  uint oldindex = HASH(leastrecent->blockno);
  //如果这个buf没有被放在哈希里面
  if(oldtick==0)
  {
    //printf("oldstick=0\n");
    leastrecent->dev=dev;
    leastrecent->blockno=blockno;
    leastrecent->refcnt=1;
    leastrecent->valid=0;
    leastrecent->tick=ticks;
    
    //放到当前的哈希里面
    if(bcache.hashbuckets[index].next!=0)
    {
      (bcache.hashbuckets[index].next)->prev=leastrecent;
    }
    
    leastrecent->next=bcache.hashbuckets[index].next;
    bcache.hashbuckets[index].next=leastrecent;
    leastrecent->prev=&(bcache.hashbuckets[index]);
    //printf("oldstic=0pass\n");
  }
  else
  {
    
    if(oldindex!=index)//这个buf在另一个哈希里面,要偷过来,
    {
      //printf("start steal\n");
      acquire(&bcache.hashlocks[oldindex]);

      leastrecent->dev=dev;
      leastrecent->blockno=blockno;
      leastrecent->refcnt=1;
      leastrecent->valid=0;
      leastrecent->tick=ticks;

      (leastrecent->prev)->next=leastrecent->next;
      if(leastrecent->next!=0)(leastrecent->next)->prev=leastrecent->prev;
      //放到当前的哈希里面
      if(bcache.hashbuckets[index].next!=0)
      {
        (bcache.hashbuckets[index].next)->prev=leastrecent;
      }
      
      leastrecent->next=bcache.hashbuckets[index].next;
      bcache.hashbuckets[index].next=leastrecent;
      leastrecent->prev=&(bcache.hashbuckets[index]);

      release(&bcache.hashlocks[oldindex]);
      //printf("oldstic=0pass\n");
      //printf("steal\n");
    }
    else//就在当前哈希里面
    {
      //printf("dongt move\n");
      leastrecent->dev=dev;
      leastrecent->blockno=blockno;
      leastrecent->refcnt=1;
      leastrecent->valid=0;
      leastrecent->tick=ticks;
    }
   
  } 
  release(&bcache.lock);
  release(&bcache.hashlocks[index]);
  acquiresleep(&leastrecent->lock);
  //printf("chenggong1\n");
  return leastrecent;
  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int index=HASH(b->blockno);
  acquire(&bcache.hashlocks[index]);

  b->refcnt--;
  if(b->refcnt==0)
  {
    //acquire(&tickslock);
    b->tick=ticks;
    //release(&tickslock);
  }
  release(&bcache.hashlocks[index]);
  // acquire(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
  // release(&bcache.lock);
}

void
bpin(struct buf *b) {
  int index=b->blockno % NBUCKET;
  acquire(&bcache.hashlocks[index]);
  b->refcnt++;
  release(&bcache.hashlocks[index]);
}

void
bunpin(struct buf *b) {
  int index=b->blockno % NBUCKET;
  acquire(&bcache.hashlocks[index]);
  b->refcnt--;
  release(&bcache.hashlocks[index]);
}


