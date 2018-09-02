#ifndef TH_GENERIC_FILE
#define TH_GENERIC_FILE "generic/VolumetricReplicationPadding.c"
#else

static inline void THNN_(VolumetricReplicationPadding_shapeCheck)(
                         THNNState *state,
                         THTensor *input,
                         THTensor *gradOutput,
                         int pleft, int pright,
                         int ptop, int pbottom,
                         int pfront, int pback) {
  int dimw = 3;
  int dimh = 2;
  int dimd = 1;
  int dimslices = 0;
  int64_t nslices;
  int64_t idepth;
  int64_t iheight;
  int64_t iwidth;
  int64_t odepth;
  int64_t oheight;
  int64_t owidth;

  THNN_ARGCHECK(!input->is_empty() && (input->dim() == 4 || input->dim() == 5), 2, input,
		"non-empty 4D or 5D (batch mode) tensor expected for input, but got: %s");

  if (input->dim() == 5)
  {
    dimw++;
    dimh++;
    dimd++;
    dimslices++;
  }

  /* sizes */
  nslices = input->size(dimslices);
  idepth = input->size(dimd);
  iheight = input->size(dimh);
  iwidth = input->size(dimw);
  odepth = idepth + pfront + pback;
  oheight = iheight + ptop + pbottom;
  owidth  = iwidth + pleft + pright;

  THArgCheck(owidth >= 1 || oheight >= 1 || odepth >= 1, 2,
             "input (D: %d H: %d, W: %d)is too small."
             " Calculated output D: %d H: %d W: %d",
             idepth, iheight, iwidth, odepth, oheight, owidth);

  if (gradOutput != NULL) {
    THArgCheck(nslices == THTensor_(size)(gradOutput, dimslices), 3,
               "gradOutput width unexpected. Expected: %d, Got: %d",
               nslices, THTensor_(size)(gradOutput, dimslices));
    THArgCheck(owidth == THTensor_(size)(gradOutput, dimw), 3,
               "gradOutput width unexpected. Expected: %d, Got: %d",
               owidth, THTensor_(size)(gradOutput, dimw));
    THArgCheck(oheight == THTensor_(size)(gradOutput, dimh), 3,
               "gradOutput height unexpected. Expected: %d, Got: %d",
               oheight, THTensor_(size)(gradOutput, dimh));
    THArgCheck(odepth == THTensor_(size)(gradOutput, dimd), 3,
               "gradOutput depth unexpected. Expected: %d, Got: %d",
               odepth, THTensor_(size)(gradOutput, dimd));
  }
}

static void THNN_(VolumetricReplicationPadding_updateOutput_frame)(
  real *input_p, real *output_p,
  int64_t nslices,
  int64_t iwidth, int64_t iheight, int64_t idepth,
  int64_t owidth, int64_t oheight, int64_t odepth,
  int pleft, int pright,
  int ptop, int pbottom,
  int pfront, int pback)
{
  int iStartX = fmax(0, -pleft);
  int iStartY = fmax(0, -ptop);
  int iStartZ = fmax(0, -pfront);
  int oStartX = fmax(0, pleft);
  int oStartY = fmax(0, ptop);
  int oStartZ = fmax(0, pfront);

  int64_t k, ip_x, ip_y, ip_z;
#pragma omp parallel for private(k, ip_x, ip_y, ip_z)
  for (k = 0; k < nslices; k++) {
    int64_t i, j, z;
    for (z = 0; z < odepth; z++) {
      for (i = 0; i < oheight; i++) {
        for (j = 0; j < owidth; j++) {
          if (j < pleft) {
            ip_x = pleft;
          } else if (j >= pleft && j < iwidth + pleft) {
            ip_x = j;
          } else {
            ip_x = iwidth + pleft - 1;
          }
          ip_x = ip_x - oStartX + iStartX;

          if (i < ptop) {
            ip_y = ptop;
          } else if (i >= ptop && i < iheight + ptop) {
            ip_y = i;
          } else {
            ip_y = iheight + ptop - 1;
          }
          ip_y = ip_y - oStartY + iStartY;

          if (z < pfront) {
            ip_z = pfront;
          } else if (z >= pfront && z < idepth + pfront) {
            ip_z = z;
          } else {
            ip_z = idepth + pfront - 1;
          }
          ip_z = ip_z - oStartZ + iStartZ;

          real *dest_p = output_p + k * owidth * oheight * odepth +
              z * owidth * oheight + i * owidth + j;
          real *src_p = input_p + k * iwidth * iheight * idepth +
              ip_z * iwidth * iheight + ip_y * iwidth + ip_x;
          *dest_p = *src_p;
        }
      }
    }
  }
}

void THNN_(VolumetricReplicationPadding_updateOutput)(THNNState *state,
                                                      THTensor *input,
                                                      THTensor *output,
                                                      int pleft, int pright,
                                                      int ptop, int pbottom,
                                                      int pfront, int pback)
{
  int dimw = 3;
  int dimh = 2;
  int dimd = 1;
  int dimslices = 0;
  int64_t nbatch = 1;
  int64_t nslices;
  int64_t idepth;
  int64_t iheight;
  int64_t iwidth;
  int64_t odepth;
  int64_t oheight;
  int64_t owidth;
  real *input_data;
  real *output_data;

THNN_(VolumetricReplicationPadding_shapeCheck)(
      state, input, NULL, pleft, pright,
      ptop, pbottom, pfront, pback);

  if (input->dim() == 5)
  {
    nbatch = input->size(0);
    dimw++;
    dimh++;
    dimd++;
    dimslices++;
  }

  /* sizes */
  nslices = input->size(dimslices);
  idepth = input->size(dimd);
  iheight = input->size(dimh);
  iwidth = input->size(dimw);
  odepth = idepth + pfront + pback;
  oheight = iheight + ptop + pbottom;
  owidth  = iwidth + pleft + pright;

  /* get contiguous input */
  input = THTensor_(newContiguous)(input);

  /* resize output */
  if (input->dim() == 4)
  {
    THTensor_(resize4d)(output, nslices, odepth, oheight, owidth);

    input_data = input->data<real>();
    output_data = output->data<real>();

    THNN_(VolumetricReplicationPadding_updateOutput_frame)(
         input_data, output_data, nslices, iwidth, iheight, idepth,
         owidth, oheight, odepth, pleft, pright, ptop, pbottom, pfront,
         pback);
  }
  else
  {
    int64_t p;

    THTensor_(resize5d)(output, nbatch, nslices, odepth, oheight, owidth);

    input_data = input->data<real>();
    output_data = output->data<real>();

#pragma omp parallel for private(p)
    for (p = 0; p < nbatch; p++)
    {
      THNN_(VolumetricReplicationPadding_updateOutput_frame)(
        input_data + p * nslices * iwidth * iheight * idepth,
        output_data + p * nslices * owidth * oheight * odepth,
        nslices,
        iwidth, iheight, idepth,
        owidth, oheight, odepth,
        pleft, pright,
        ptop, pbottom,
        pfront, pback);
    }
  }

  /* cleanup */
  c10::raw::intrusive_ptr::decref(input);
}

static void THNN_(VolumetricReplicationPadding_updateGradInput_frame)(
  real *ginput_p, real *goutput_p,
  int64_t nslices,
  int64_t iwidth, int64_t iheight, int64_t idepth,
  int64_t owidth, int64_t oheight, int64_t odepth,
  int pleft, int pright,
  int ptop, int pbottom,
  int pfront, int pback)
{
  int iStartX = fmax(0, -pleft);
  int iStartY = fmax(0, -ptop);
  int iStartZ = fmax(0, -pfront);
  int oStartX = fmax(0, pleft);
  int oStartY = fmax(0, ptop);
  int oStartZ = fmax(0, pfront);

  int64_t k, ip_x, ip_y, ip_z;
#pragma omp parallel for private(k, ip_x, ip_y, ip_z)
  for (k = 0; k < nslices; k++) {
    int64_t i, j, z;
    for (z = 0; z < odepth; z++) {
      for (i = 0; i < oheight; i++) {
        for (j = 0; j < owidth; j++) {
          if (j < pleft) {
            ip_x = pleft;
          } else if (j >= pleft && j < iwidth + pleft) {
            ip_x = j;
          } else {
            ip_x = iwidth + pleft - 1;
          }
          ip_x = ip_x - oStartX + iStartX;

          if (i < ptop) {
            ip_y = ptop;
          } else if (i >= ptop && i < iheight + ptop) {
            ip_y = i;
          } else {
            ip_y = iheight + ptop - 1;
          }
          ip_y = ip_y - oStartY + iStartY;

          if (z < pfront) {
            ip_z = pfront;
          } else if (z >= pfront && z < idepth + pfront) {
            ip_z = z;
          } else {
            ip_z = idepth + pfront - 1;
          }
          ip_z = ip_z - oStartZ + iStartZ;

          real *src_p = goutput_p + k * owidth * oheight * odepth +
              z * owidth * oheight + i * owidth + j;
          real *dest_p = ginput_p + k * iwidth * iheight * idepth +
              ip_z * iwidth * iheight + ip_y * iwidth + ip_x;
          *dest_p += *src_p;
        }
      }
    }
  }
}

void THNN_(VolumetricReplicationPadding_updateGradInput)(THNNState *state,
                                                         THTensor *input,
                                                         THTensor *gradOutput,
                                                         THTensor *gradInput,
                                                         int pleft, int pright,
                                                         int ptop, int pbottom,
                                                         int pfront, int pback)
{
  int dimw = 3;
  int dimh = 2;
  int dimd = 1;
  int dimslices = 0;
  int64_t nbatch = 1;
  int64_t nslices;
  int64_t idepth;
  int64_t iheight;
  int64_t iwidth;
  int64_t odepth;
  int64_t oheight;
  int64_t owidth;

  if (input->dim() == 5)
  {
    nbatch = input->size(0);
    dimw++;
    dimh++;
    dimd++;
    dimslices++;
  }

  /* sizes */
  nslices = input->size(dimslices);
  idepth = input->size(dimd);
  iheight = input->size(dimh);
  iwidth = input->size(dimw);
  odepth = idepth + pfront + pback;
  oheight = iheight + ptop + pbottom;
  owidth  = iwidth + pleft + pright;


THNN_(VolumetricReplicationPadding_shapeCheck)(
      state, input, NULL, pleft, pright,
      ptop, pbottom, pfront, pback);

  /* get contiguous gradOutput */
  gradOutput = THTensor_(newContiguous)(gradOutput);

  /* resize */
  THTensor_(resizeAs)(gradInput, input);
  THTensor_(zero)(gradInput);

  /* backprop */
  if (input->dim() == 4) {
    THNN_(VolumetricReplicationPadding_updateGradInput_frame)(
      gradInput->data<real>(),
      gradOutput->data<real>(),
      nslices,
      iwidth, iheight, idepth,
      owidth, oheight, odepth,
      pleft, pright,
      ptop, pbottom,
      pfront, pback);
  } else {
    int64_t p;
#pragma omp parallel for private(p)
    for (p = 0; p < nbatch; p++) {
      THNN_(VolumetricReplicationPadding_updateGradInput_frame)(
        gradInput->data<real>() + p * nslices * idepth * iheight * iwidth,
        gradOutput->data<real>() + p * nslices * odepth * oheight * owidth,
        nslices,
        iwidth, iheight, idepth,
        owidth, oheight, odepth,
        pleft, pright,
        ptop, pbottom,
        pfront, pback);
    }
  }

  /* cleanup */
  c10::raw::intrusive_ptr::decref(gradOutput);
}

#endif
