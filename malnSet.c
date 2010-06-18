#include "malnSet.h"
#include "malnComp.h"
#include "malnBlk.h"
#include "mafTree.h"
#include "sonLibSortedSet.h"
#include "common.h"
#include "jkmaf.h"
#include "genomeRangeTree.h"

struct malnSet {
    struct Genomes *genomes;
    struct Genome *refGenome;
    stSortedSet *blks;
    struct genomeRangeTree *refCompMap; /* range index slRefs of reference  malnComp objects.  Built on
                                         * demand. */
};

/* Add all reference genome components. */
static void addRefCompsToMap(struct malnSet *malnSet, struct malnBlk *blk) {
    for (struct malnComp *comp = blk->comps; comp != NULL; comp = comp->next) {
        if (comp->seq->genome == malnSet->refGenome) {
            genomeRangeTreeAddValList(malnSet->refCompMap, comp->seq->name, comp->chromStart, comp->chromEnd, slRefNew(comp));
        }
    }
}

/* build the range tree when needed */
static void buildRangeTree(struct malnSet *malnSet) {
    malnSet->refBlks = genomeRangeTreeNew();
    stSortedSetIterator *iter = stSortedSet_getIterator(malnSet->blks);
    struct malnBlk *blk;
    while ((blk = stSortedSet_getNext(iter)) != NULL) {
        addRefRanges(malnSet, blk);
    }
    stSortedSet_destructIterator(iter);
}

/* remove a single object from the range tree (missing genomeRangeTree functionality).
 * this just set reference to NULL, which must be handled when getting overlaps*/
static void removeCompRange(struct malnSet *malnSet, struct malnBlk *blk, struct malnComp *comp) {
    bool found = false;
    for (struct range *rng = genomeRangeTreeAllOverlapping(malnSet->refBlks, comp->seq->name, comp->chromStart, comp->chromEnd); (rng != NULL) && (!found); rng = rng->next) {
        for (struct slRef *compRef = rng->val; (compRef != NULL; compRef = compRef->next) {
            struct malnComp = compRef->val;
        if (blkRef->val == blk) {
            blkRef->val = NULL;
            found = true;
        }
    }
    assert(found);
}

/* remove all references to this blk from the rangeTree */
static void removeRefRanges(struct malnSet *malnSet, struct malnBlk *blk) {
    for (struct malnComp *comp = blk->comps; comp != NULL; comp = comp->next) {
        if (comp->seq->genome == malnSet->refGenome) {
            removeCompRange(malnSet, blk, comp);
        }
    }
}

/* convert a mafComp to an malnComp */
static struct malnComp *mafCompToMAlnComp(struct Genomes *genomes, struct mafComp *comp) {
    char buf[128];
    char *srcDb = mafCompGetSrcDb(comp, buf, sizeof(buf));
    if (srcDb == NULL) {
        errAbort("Error: no org name in MAF component, source must be org.seq: %s", comp->src);
    }
    return malnComp_construct(genomesObtainSeq(genomes, srcDb, mafCompGetSrcName(comp), comp->srcSize),
                                comp->strand, comp->start, comp->start+comp->size, comp->text);
}

/* convert a mafAli to an malnBlk */
static struct malnBlk *mafAliToMAlnBlk(struct Genomes *genomes, struct Genome *refGenome, struct mafAli *ali, double defaultBranchLength) {
    struct malnBlk *blk = malnBlk_construct(mafTree_constructFromMaf(ali, defaultBranchLength));
    for (struct mafComp *comp = ali->components; comp != NULL; comp = comp->next) {
        malnBlk_addComp(blk, mafCompToMAlnComp(genomes, comp));
    }
    malnBlk_sortComps(blk);
    malnBlk_setLocAttr(blk, refGenome);
    return blk;
}

/* get associated genomes object  */
struct Genomes *malnSet_getGenomes(struct malnSet *malnSet) {
    return malnSet->genomes;
}

/* get reference genome object  */
struct Genome *malnSet_getRefGenome(struct malnSet *malnSet) {
    return malnSet->refGenome;
}

/* add a block to a malnSet */
void malnSet_addBlk(struct malnSet *malnSet, struct malnBlk *blk) {
    assert(blk->malnSet == NULL);
    malnBlk_assert(blk);
    blk->malnSet = malnSet;
    slAddHead(&malnSet->blks, blk);
}

/* remove a block from malnSet */
void malnSet_removeBlk(struct malnSet *malnSet, struct malnBlk *blk) {
    assert(blk->malnSet != NULL);
    if (malnSet->refBlks != NULL) {
        removeRefRanges(malnSet, blk);
    }
    stSortedSet_remove(malnSet->blks, blk);
    blk->malnSet = NULL;
}

/* remove a block from malnSet and free the block */
void malnSet_deleteBlk(struct malnSet *malnSet, struct malnBlk *blk) {
    malnSet_removeBlk(malnSet, blk);
    malnBlk_destruct(blk);
}

/* construct an empty malnSet  */
struct malnSet *malnSet_construct(struct Genomes *genomes, struct Genome *refGenome) {
    struct malnSet *malnSet;
    AllocVar(malnSet);
    malnSet->genomes = genomes;
    malnSet->refGenome = refGenome;
    malnSet->blks = stSortedSet_construct();
    return malnSet;
}

/* Construct a malnSet from a MAF file. defaultBranchLength is used to
 * assign branch lengths when inferring trees from pair-wise MAFs. */
struct malnSet *malnSet_constructFromMaf(struct Genomes *genomes, struct Genome *refGenome, char *mafFileName, double defaultBranchLength) {
    struct malnSet *malnSet = malnSet_construct(genomes, refGenome);
    struct mafFile *mafFile = mafOpen(mafFileName);
    struct mafAli *ali;
    while ((ali = mafNext(mafFile)) != NULL) {
        malnSet_addBlk(malnSet, mafAliToMAlnBlk(genomes, refGenome, ali, defaultBranchLength));
        mafAliFree(&ali);
    }
    slReverse(&malnSet->blks);
    return malnSet;
}

/* convert a malnComp to a mafComp */
static struct mafComp *malnCompToMafComp(struct malnComp *comp) {
    struct mafComp *mc;
    AllocVar(mc);
    mc->src = cloneString(comp->seq->orgSeqName);
    mc->srcSize = comp->seq->size;
    mc->strand = comp->strand;
    mc->start = comp->start;
    mc->size = comp->end - comp->start;
    mc->text = cloneString(malnComp_getAln(comp));
    return mc;
}

/* convert a malnBlk to a mafAli */
static struct mafAli *malnAliToMafAli(struct malnBlk *blk) {
    malnBlk_assert(blk);
    struct mafAli *ma;
    AllocVar(ma);
    if (blk->mTree != NULL) {
        ma->tree = mafTree_format(blk->mTree);
    }
    ma->textSize = blk->alnWidth;
    for (struct malnComp *comp = blk->comps; comp != NULL; comp = comp->next) {
        slAddHead(&ma->components, malnCompToMafComp(comp));
    }
    slReverse(&ma->components);
    return ma;
}

/* compare the root components on two blocks */
static int blkCmpRootComp(const void *vblk1, const void *vblk2) {
    struct malnBlk *blk1 = (struct malnBlk *)vblk1;
    struct malnBlk *blk2 = (struct malnBlk *)vblk2;
    struct malnComp *root1 = slLastEl(blk1->comps);
    struct malnComp *root2 = slLastEl(blk2->comps);
    int diff = strcmp(root1->seq->genome->name, root2->seq->genome->name);
    if (diff == 0) {
        diff = strcmp(root1->seq->name, root2->seq->name);
    }
    if (diff == 0) {
        diff = root1->chromStart - root2->chromStart;
    }
    if (diff == 0) {
        diff = root1->chromEnd - root2->chromEnd;
    }
    return diff;
}

/* build a set of the blocks, sorted by root components */
static stSortedSet *buildRootSortSet(struct malnSet *malnSet) {
    stSortedSet *sorted = stSortedSet_construct3(blkCmpRootComp, NULL);
    stSortedSetIterator *iter = stSortedSet_getIterator(malnSet->blks);
    struct malnBlk *blk;
    while ((blk = stSortedSet_getNext(iter)) != NULL) {
        stSortedSet_insert(sorted, blk);
    }
    stSortedSet_destructIterator(iter);
    return sorted;
}

/* write a block to a MAF */
static void writeBlkToMaf(struct malnBlk *blk, FILE *mafFh) {
    struct mafAli *ma = malnAliToMafAli(blk);
    mafWrite(mafFh, ma);
    mafAliFree(&ma);
}

/* write a malnSet to a MAF file  */
void malnSet_writeMaf(struct malnSet *malnSet, char *mafFileName) {
    stSortedSet *sorted = buildRootSortSet(malnSet);

    FILE *mafFh = mustOpen(mafFileName, "w");
    mafWriteStart(mafFh, NULL);
    stSortedSetIterator *iter = stSortedSet_getIterator(malnSet->blks);
    struct malnBlk *blk;
    while ((blk = stSortedSet_getNext(iter)) != NULL) {
        writeBlkToMaf(blk, mafFh);
    }
    stSortedSet_destructIterator(iter);
    mafWriteEnd(mafFh);
    carefulClose(&mafFh);
    stSortedSet_destruct(sorted);
}

/* get iterator of the blocks. Don't remove or add blocks while in motion. */
stSortedSetIterator *malnSet_getBlocks(struct malnSet *malnSet) {
    return stSortedSet_getIterator(malnSet->blks);
}

/* get list of slRefs to blks who's reference range overlaps the specified
 * range. */
stSortedSet *malnSet_getOverlapping(struct malnSet *malnSet, struct Seq *seq, int chromStart, int chromEnd) {
    if (malnSet->refBlks == NULL) {
        buildRangeTree(malnSet);
    }
    stSortedSet *overBlks = stSortedSet_construct();
    for (struct range *rng = genomeRangeTreeAllOverlapping(malnSet->refBlks, seq->name, chromStart, chromEnd); rng != NULL; rng = rng->next) {
        struct slRef *blkRef = rng->val;
        if (blkRef->val != NULL) {
            stSortedSet_insert(overBlks, blkRef->val);
        }
    }
    return overBlks;
}

/* clear done flag on all blocks */
void malnSet_clearDone(struct malnSet *malnSet) {
    stSortedSetIterator *iter = stSortedSet_getIterator(malnSet->blks);
    struct malnBlk *blk;
    while ((blk = stSortedSet_getNext(iter)) != NULL) {
        blk->done = false;
    }
    stSortedSet_destructIterator(iter);
}

