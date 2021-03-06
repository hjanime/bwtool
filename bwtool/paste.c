/* bwtool_paste - simultaneously output same regions of multiple bigWigs */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif 

#include "common.h"
#include "obscure.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "sqlNum.h"
#include "basicBed.h"
#include "bigWig.h"
#include "bigs.h"
#include "bwtool.h"
#include "cluster.h"
#include "bwtool_shared.h"

void usage_paste()
/* Explain usage of paste program and exit. */
{
errAbort(
  "bwtool paste - simultaneously output same regions of multiple bigWigs\n"
  "usage:\n"
  "   bwtool paste input1.bw input2.bw input3.bw ...\n"
  "options:\n"
  "   -header           put header with labels from file or filenames\n"
  "   -consts=c1,c2...  add constants to output lines\n"
  "   -skip-NA          don't output lines (bases) where one of the inputs is NA\n"
  );
}

void print_line(struct perBaseWig *pbw_list, struct slDouble *c_list, int decimals, enum wigOutType wot, int i, FILE *out)
{
    struct perBaseWig *pbw;
    struct slDouble *c;
    if (wot == bedGraphOut)
	fprintf(out, "%s\t%d\t%d\t", pbw_list->chrom, pbw_list->chromStart+i, pbw_list->chromStart+i+1);
    else if (wot == varStepOut)
	fprintf(out, "%d\t", pbw_list->chromStart+i+1);
    for (pbw = pbw_list; pbw != NULL; pbw = pbw->next)
    {
	if (isnan(pbw->data[i]))
	    fprintf(out, "NA");
	else
	    fprintf(out, "%0.*f", decimals, pbw->data[i]);
	fprintf(out, "%c", (c_list == NULL) && (pbw->next == NULL) ? '\n' : '\t');
    }
    for (c = c_list; c != NULL; c = c->next)
	fprintf(out, "%0.*f%c", decimals, c->val, (c->next == NULL) ? '\n' : '\t');
}

boolean has_na(struct perBaseWig *pbw_list, int i)
{
    struct perBaseWig *pbw;
    for (pbw = pbw_list; pbw != NULL; pbw = pbw->next)
	if (isnan(pbw->data[i]))
	    return TRUE;
    return FALSE;
}

void output_pbws(struct perBaseWig *pbw_list, struct slDouble *c_list, int decimals, enum wigOutType wot, boolean skip_NA, FILE *out)
/* outputs one set of perBaseWigs all at the same section */
{
    struct perBaseWig *pbw;
    if (pbw_list)
    {
	boolean last_skipped = TRUE;
	int i = 0;
	int last_printed = -2;
	for (i = 0; i < pbw_list->len; i++)
	{
	    if (!skip_NA || !has_na(pbw_list, i))
	    {
		if (i - last_printed > 1)
		{
		    if (wot == varStepOut)
			fprintf(out, "variableStep chrom=%s span=1\n", pbw_list->chrom);
		    else if (wot == fixStepOut)
			fprintf(out, "fixedStep chrom=%s start=%d step=1 span=1\n", pbw_list->chrom, pbw_list->chromStart+i+1);
		}
		print_line(pbw_list, c_list, decimals, wot, i, out);
		last_printed = i;
	    }
	}
    }
}

struct slDouble *parse_constants(char *consts)
/* simply process the comma-list of constants from the command and return the list*/
{
    if (!consts)
	return NULL;
    struct slName *strings = slNameListFromComma(consts);
    struct slName *s;
    struct slDouble *c_list = NULL;
    for (s = strings; s != NULL; s = s->next)
    {
	struct slDouble *d = slDoubleNew(sqlDouble(s->name));
	slAddHead(&c_list, d);
    }
    slReverse(&c_list);
    slFreeList(&strings);
    return c_list;
}

void bwtool_paste(struct hash *options, char *favorites, char *regions, unsigned decimals, double fill, 
		  enum wigOutType wot, struct slName **p_files, char *output_file)
/* bwtool_paste - main for paste program */
{
    struct metaBig *mb;
    struct metaBig *mb_list = NULL;
    struct bed *bed; 
    struct slName *file;
    int num_sections = 0;
    int i = 0;
    boolean skip_na = (hashFindVal(options, "skip-NA") != NULL) ? TRUE : FALSE;
    if (!isnan(fill) && skip_na)
	errAbort("cannot use -skip_na with -fill");
    boolean header = (hashFindVal(options, "header") != NULL) ? TRUE : FALSE;
    boolean verbose = (hashFindVal(options, "verbose") != NULL) ? TRUE : FALSE;
    struct slDouble *c_list = parse_constants((char *)hashOptionalVal(options, "consts", NULL));
    struct slName *labels = NULL;
    struct slName *files = *p_files;
    FILE *out = (output_file) ? mustOpen(output_file, "w") : stdout;
    /* open the files one by one */
    if (slCount(files) == 1)
	check_for_list_files(&files, &labels);
    for (file = files; file != NULL; file = file->next)
    {
	mb = metaBigOpen(file->name, regions);
	slAddHead(&mb_list, mb);
    }
    slReverse(&mb_list);
    num_sections = slCount(mb_list->sections);
    if (header)
    {
	printf("#chrom\tchromStart\tchromEnd");
	if (labels)
	{
	    struct slName *label;
	    for (label = labels; label != NULL; label = label->next)
		printf("\t%s", label->name);
	}
	else
	    for (mb = mb_list; mb != NULL; mb = mb->next)
		printf("\t%s", mb->fileName);
	printf("\n");
    }
    for (bed = mb_list->sections; bed != NULL; bed = bed->next)
    {
	struct perBaseWig *pbw_list = NULL;
	/* load each region */
	if (verbose)
	    fprintf(stderr, "section %d / %d: %s:%d-%d\n", i++, num_sections, bed->chrom, bed->chromStart, bed->chromEnd);
	for (mb = mb_list; mb != NULL; mb = mb->next)
	{
	    struct perBaseWig *pbw = perBaseWigLoadSingleContinue(mb, bed->chrom, bed->chromStart, bed->chromEnd, FALSE, fill);
	    /* if the load returns null then NA the whole thing. */
	    /* this isn't very efficient but it's the easy way out. */
	    if (!pbw)
		pbw = alloc_perBaseWig(bed->chrom, bed->chromStart, bed->chromEnd);
	    slAddHead(&pbw_list, pbw);
	}
	slReverse(&pbw_list);
	output_pbws(pbw_list, c_list, decimals, wot, skip_na, out);
	perBaseWigFreeList(&pbw_list);
    }
    /* close the files */
    carefulClose(&out);
    while ((mb = slPopHead(&mb_list)) != NULL)
	metaBigClose(&mb);
    if (labels)
	slNameFreeList(&labels);
    slNameFreeList(p_files);
    if (c_list)
	slFreeList(&c_list);
}
