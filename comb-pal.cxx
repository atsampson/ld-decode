/* LD decoder prototype, Copyright (C) 2013 Chad Page.  License: LGPL2 */


#include "ld-decoder.h"
#include "deemp.h"

int ofd = 1;
char *image_base = "FRAME";

bool f_write8bit = false;
bool f_pulldown = false;
bool f_writeimages = false;
bool f_training = false;
bool f_bw = false;
bool f_debug2d = false;
bool f_adaptive2d = true;
bool f_oneframe = false;
bool f_showk = false;
bool f_wide = false;

bool f_colorlpf = false;
bool f_colorlpf_hq = true;

double nn_cscale = 32768.0;

double p_3dcore = -1;
double p_3drange = -1;
double p_2dcore = -1;
double p_2drange = -1;
double p_3d2drej = 2;

int f_debugline = -1000;
	
int dim = 2;

//const double freq = 4.0;

double irescale = 376.32;
double irebase = 0;
inline uint16_t ire_to_u16(double ire);

// tunables

int lineoffset = 32;

int linesout = 576;

double brightness = 240;

double black_ire = 0;
int black_u16 = ire_to_u16(black_ire);
int white_u16 = ire_to_u16(100 + black_ire); 

double nr_c = 0.0;
double nr_y = 1.0;

inline double IRE(double in) 
{
	return (in * 140.0) - 40.0;
}

// XXX:  This is actually YUV.
struct YIQ {
        double y, i, q;

        YIQ(double _y = 0.0, double _i = 0.0, double _q = 0.0) {
                y = _y; i = _i; q = _q;
        };

	YIQ operator*=(double x) {
		YIQ o;

		o.y = this->y * x;
		o.i = this->i * x;
		o.q = this->q * x;

		return o;
	}
	
	YIQ operator+=(YIQ p) {
		YIQ o;

		o.y = this->y + p.y;
		o.i = this->i + p.i;
		o.q = this->q + p.q;

		return o;
	}
};

double clamp(double v, double low, double high)
{
        if (v < low) return low;
        else if (v > high) return high;
        else return v;
}

inline double u16_to_ire(uint16_t level)
{
	if (level == 0) return -100;
	
	return -43.122874 + ((double)(level - irebase) / irescale); 
}

int cline = -1;

struct RGB {
        double r, g, b;

        void conv(YIQ _y) {
               YIQ t;

		double y = u16_to_ire(clamp(_y.y, 0, 65535));
		y = (y - black_ire) * (100 / (100 - black_ire)); 

		double u = +(_y.i) / irescale;
		double v = +(_y.q) / irescale;

                r = y + (1.13983 * v);
                g = y - (0.58060 * v) - (u * 0.39465);
                b = y + (u * 2.032);

		double m = brightness * 255 / 100;

                r = clamp(r * m, 0, 65535);
                g = clamp(g * m, 0, 65535);
                b = clamp(b * m, 0, 65535);
     };
};

inline uint16_t ire_to_u16(double ire)
{
	if (ire <= -50) return 0;
	
	return clamp(((ire + 43.122874) * irescale) + irebase, 1, 65535);
} 

int write_locs = -1;

const int nframes = 3;	// 3 frames needed for 3D buffer - for now

const int in_y = 610;
const int in_x = 1052;
//const int in_size = in_y * in_x;

typedef struct cline {
	YIQ p[in_x];
} cline_t;

struct frame_t {
	uint16_t rawbuffer[in_x * in_y];

	double clpbuffer[3][in_y][in_x];
	double combk[3][in_y][in_x];
		
	cline_t cbuf[in_y];
};

class Comb
{
	protected:
		int linecount;  // total # of lines process - needed to maintain phase
		int curline;    // current line # in frame 

		int framecode;
		int framecount;	

		bool f_oddframe;	// true if frame starts with odd line

		long long scount;	// total # of samples consumed

		int fieldcount;
		int frames_out;	// total # of written frames
	
		uint16_t output[in_x * in_y * 3];
		uint16_t BGRoutput[in_x * in_y * 3];
		uint16_t obuf[in_x * in_y * 3];
		
		uint16_t Goutput[in_x * in_y];
		uint16_t Flowmap[in_x * in_y];

		double aburstlev;	// average color burst

		cline_t tbuf[in_y];
		cline_t pbuf[in_y], nbuf[in_y];

		frame_t Frame[nframes];

		IIRFilter<25, 1> *f_hpy, *f_hpvy;
		IIRFilter<17, 1> *f_hpi, *f_hpq, *f_hpvi, *f_hpvq;

		void FilterIQ(cline_t cbuf[in_y], int fnum) {
			for (int l = 24; l < in_y; l++) {
				auto f_i(f_colorlpi);
				auto f_q(f_colorlpf_hq ? f_colorlpi : f_colorlpq);

				int qoffset = f_colorlpf_hq ? f_colorlpi_offset : f_colorlpq_offset;

				double filti = 0, filtq = 0;

				for (int h = 4; h < (in_x - 4); h++) {
					int phase = h % 4;
					
					switch (phase) {
						case 0: filti = f_i.feed(cbuf[l].p[h].i); break;
						case 1: filtq = f_q.feed(cbuf[l].p[h].q); break;
						case 2: filti = f_i.feed(cbuf[l].p[h].i); break;
						case 3: filtq = f_q.feed(cbuf[l].p[h].q); break;
						default: break;
					}

					if (l == (f_debugline + lineoffset)) {
						cerr << "IQF " << h << ' ' << cbuf[l].p[h - f_colorlpi_offset].i << ' ' << filti << ' ' << cbuf[l].p[h - qoffset].q << ' ' << filtq << endl;
					}

					cbuf[l].p[h - f_colorlpi_offset].i = filti; 
					cbuf[l].p[h - qoffset].q = filtq; 
				}
			}
		}
	
		// precompute 1D comb filter, used as a fallback for edges 
		void Split1D(int fnum)
		{
			for (int l = 24; l < in_y; l++) {
				uint16_t *line = &Frame[fnum].rawbuffer[l * in_x];	
				bool invertphase = false; // (line[0] == 16384);

				auto f_1di(f_colorlpi);
				auto f_1dq(f_colorlpq);
				int f_toffset = 8;

				for (int h = 4; h < (in_x - 4); h++) {
					int phase = h % 4;
					double tc1 = (((line[h + 2] + line[h - 2]) / 2) - line[h]); 
					double tc1f = 0, tsi = 0, tsq = 0;

					if (!invertphase) tc1 = -tc1;
						
					switch (phase) {
						case 0: tsi = tc1; tc1f = f_1di.feed(tsi); break;
						case 1: tsq = -tc1; tc1f = -f_1dq.feed(tsq); break;
						case 2: tsi = -tc1; tc1f = -f_1di.feed(tsi); break;
						case 3: tsq = tc1; tc1f = f_1dq.feed(tsq); break;
						default: break;
					}
						
					if (!invertphase) {
						tc1 = -tc1;
						tc1f = -tc1f;
					}

					Frame[fnum].clpbuffer[0][l][h] = tc1;
//					if (dim == 1) Frame[fnum].clpbuffer[0][l][h - f_toffset] = tc1f;

					Frame[fnum].combk[0][l][h] = 1;

					if (l == (f_debugline + lineoffset)) {
						cerr << h << ' ' << line[h - 4] << ' ' << line[h - 2] << ' ' << line[h] << ' ' << line[h + 2] << ' ' << line[h + 4] << ' ' << tc1 << ' ' << Frame[fnum].clpbuffer[0][l][h - f_toffset] << endl;
					}
				}
			}
		}
	
		int rawbuffer_val(int fr, int x, int y) {
			return Frame[fr].rawbuffer[(y * in_x) + x];
		}
	
		void Split2D(int f) 
		{
			for (int l = 24; l < in_y; l++) {
				uint16_t *pline = &Frame[f].rawbuffer[(l - 4) * in_x];	
				uint16_t *line = &Frame[f].rawbuffer[l * in_x];	
				uint16_t *nline = &Frame[f].rawbuffer[(l + 4) * in_x];	
		
				double *p1line = Frame[f].clpbuffer[0][l - 4];
				double *c1line = Frame[f].clpbuffer[0][l];
				double *n1line = Frame[f].clpbuffer[0][l + 4];
		
				// 2D filtering.  can't do top or bottom line - calced between 1d and 3d because this is
				// filtered 
				if ((l >= 4) && (l <= (in_y - 4))) {
					for (int h = 18; h < (in_x - 4); h++) {
						double tc1;
					
						double kp, kn;

						kp  = fabs(fabs(c1line[h]) - fabs(p1line[h])); // - fabs(c1line[h] * .20);
						kp += fabs(fabs(c1line[h - 1]) - fabs(p1line[h - 1])); 
						kp -= (fabs(c1line[h]) + fabs(c1line[h - 1])) * .10;
						kn  = fabs(fabs(c1line[h]) - fabs(n1line[h])); // - fabs(c1line[h] * .20);
						kn += fabs(fabs(c1line[h - 1]) - fabs(n1line[h - 1])); 
						kn -= (fabs(c1line[h]) + fabs(n1line[h - 1])) * .10;

						kp /= 2;
						kn /= 2;

						p_2drange = 45 * irescale;
						kp = clamp(1 - (kp / p_2drange), 0, 1);
						kn = clamp(1 - (kn / p_2drange), 0, 1);

						if (!f_adaptive2d) kn = kp = 1.0;

						double sc = 1.0;

						if (kn || kp) {	
							if (kn > (3 * kp)) kp = 0;
							else if (kp > (3 * kn)) kn = 0;

							sc = (2.0 / (kn + kp));// * max(kn * kn, kp * kp);
							if (sc < 1.0) sc = 1.0;
						} else {
							if ((fabs(fabs(p1line[h]) - fabs(n1line[h])) - fabs((n1line[h] + p1line[h]) * .2)) <= 0) {
								kn = kp = 1;
							}
						}
						

						tc1  = ((Frame[f].clpbuffer[0][l][h] - p1line[h]) * kp * sc);
						tc1 += ((Frame[f].clpbuffer[0][l][h] - n1line[h]) * kn * sc);
						tc1 /= (2 * 2);

						if (l == (f_debugline + lineoffset)) {
							//cerr << "2D " << h << ' ' << clpbuffer[l][h] << ' ' << p1line[h] << ' ' << n1line[h] << endl;
							cerr << "2D " << h << ' ' << ' ' << sc << ' ' << kp << ' ' << kn << ' ' << (pline[h]) << '|' << (p1line[h]) << ' ' << (line[h]) << '|' << (Frame[f].clpbuffer[0][l][h]) << ' ' << (nline[h]) << '|' << (n1line[h]) << " OUT " << (tc1) << endl;
						}	

						Frame[f].clpbuffer[1][l][h] = tc1;
						Frame[f].combk[1][l][h] = 1.0; // (sc * (kn + kp)) / 2.0;
					}
				}

				for (int h = 4; h < (in_x - 4); h++) {
					if ((l >= 2) && (l <= 502)) {
						Frame[f].combk[1][l][h] *= 1 - Frame[f].combk[2][l][h];
					}
					
					// 1D 
					Frame[f].combk[0][l][h] = 1 - Frame[f].combk[2][l][h] - Frame[f].combk[1][l][h];
				}
			}	
		}	

		void SplitIQ(int f) {
			double mse = 0.0;
			double me = 0.0;

			memset(Frame[f].cbuf, 0, sizeof(cline_t) * in_y); 

			for (int l = 24; l < in_y; l++) {
				double msel = 0.0, sel = 0.0;
				uint16_t *line = &Frame[f].rawbuffer[l * in_x];	
				bool invertphase = (line[0] == 16384);

//				if (f_neuralnet) invertphase = true;

				double si = 0, sq = 0;
				for (int h = 4; h < (in_x - 4); h++) {
					int phase = h % 4;
					double cavg = 0;

					cavg += (Frame[f].clpbuffer[2][l][h] * Frame[f].combk[2][l][h]);
					cavg += (Frame[f].clpbuffer[1][l][h] * Frame[f].combk[1][l][h]);
					cavg += (Frame[f].clpbuffer[0][l][h] * Frame[f].combk[0][l][h]);

					cavg /= 2;
					
					if (f_debug2d) {
						cavg = Frame[f].clpbuffer[1][l][h] - Frame[f].clpbuffer[2][l][h];
						msel += (cavg * cavg);
						sel += fabs(cavg);

						if (l == (f_debugline + lineoffset)) {
							cerr << "D2D " << h << ' ' << Frame[f].clpbuffer[1][l][h] << ' ' << Frame[f].clpbuffer[2][l][h] << ' ' << cavg << endl;
						}
					}

					if (!invertphase) cavg = -cavg;

					switch (phase) {
						case 0: si = cavg; break;
						case 1: sq = -cavg; break;
						case 2: si = -cavg; break;
						case 3: sq = cavg; break;
						default: break;
					}

					Frame[f].cbuf[l].p[h].y = line[h]; 
					if (f_debug2d) Frame[f].cbuf[l].p[h].y = ire_to_u16(50); 
					Frame[f].cbuf[l].p[h].i = si;  
					Frame[f].cbuf[l].p[h].q = sq; 
					
//					if (l == 240 ) {
//						cerr << h << ' ' << Frame[f].combk[1][l][h] << ' ' << Frame[f].combk[0][l][h] << ' ' << Frame[f].cbuf[l].p[h].y << ' ' << si << ' ' << sq << endl;
//					}

					if (f_bw) {
						Frame[f].cbuf[l].p[h].i = Frame[f].cbuf[l].p[h].q = 0;  
					}
				}

				if (f_debug2d && (l >= 6) && (l <= 500)) {
					cerr << l << ' ' << msel / (in_x - 4) << " ME " << sel / (in_x - 4) << endl; 
					mse += msel / (in_x - 4);
					me += sel / (in_x - 4);
				}
			}
			if (f_debug2d) {
				cerr << "TOTAL MSE " << mse << " ME " << me << endl;
			}
		}
		
		void DoCNR(int f, cline_t cbuf[in_y], double min = -1.0) {
			int firstline = (linesout == in_y) ? 0 : lineoffset;
	
			if (nr_c < min) nr_c = min;
			if (nr_c <= 0) return;

			for (int l = firstline; l < in_y; l++) {
				YIQ hplinef[in_x + 32];
				cline_t *input = &cbuf[l];

				for (int h = 60; h <= (in_x - 4); h++) {
					hplinef[h].i = f_hpi->feed(input->p[h].i);
					hplinef[h].q = f_hpq->feed(input->p[h].q);
				}
				
				for (int h = 60; h < (in_x - 16); h++) {
					double ai = hplinef[h + 12].i;
					double aq = hplinef[h + 12].q;

//					if (l == (f_debugline + lineoffset)) {
//						cerr << "NR " << h << ' ' << input->p[h].y << ' ' << hplinef[h + 12].y << ' ' << ' ' << a << ' ' << endl;
//					}

					if (fabs(ai) > nr_c) {
						ai = (ai > 0) ? nr_c : -nr_c;
					}
					
					if (fabs(aq) > nr_c) {
						aq = (aq > 0) ? nr_c : -nr_c;
					}

					input->p[h].i -= ai;
					input->p[h].q -= aq;
//					if (l == (f_debugline + lineoffset)) cerr << a << ' ' << input->p[h].y << endl; 
				}
			}
		}
					
		void DoYNR(int f, cline_t cbuf[in_y], double min = -1.0) {
			int firstline = (linesout == in_y) ? 0 : lineoffset;

			if (nr_y < min) nr_y = min;

			if (nr_y <= 0) return;

			for (int l = firstline; l < in_y; l++) {
				YIQ hplinef[in_x + 32];
				cline_t *input = &cbuf[l];

				for (int h = 40; h <= in_x; h++) {
					hplinef[h].y = f_hpy->feed(input->p[h].y);
				}
				
				for (int h = 40; h < in_x - 12; h++) {
					double a = hplinef[h + 12].y;

					if (l == (f_debugline + lineoffset)) {
						cerr << "NR " << l << ' ' << h << ' ' << input->p[h].y << ' ' << hplinef[h + 12].y << ' ' << ' ' << a << ' ' << endl;
					}

					if (fabs(a) > nr_y) {
						a = (a > 0) ? nr_y : -nr_y;
					}

					input->p[h].y -= a;
					if (l == (f_debugline + lineoffset)) cerr << a << ' ' << input->p[h].y << endl; 
				}
			}
		}
		
		void ToRGB(int f, int firstline, cline_t cbuf[in_y]) {
			// YIQ (YUV?) -> RGB conversion	

			double angle[in_y];

			// HACK!!!:  Need to figure out which phase we're in this frame
			for (int l = 10; l < in_y; l++) {
				double i = 0, q = 0;

				for (int h = 25; h < 55; h++) {
					YIQ yiq = cbuf[l].p[h];

					i += yiq.i; q += yiq.q;
					if (l == (f_debugline + lineoffset)) 
						cerr << "BIQ " << l << ' ' << h << ' ' << yiq.q << ' ' << yiq.i << endl;
//					cerr << "BURST " << l << ' ' << h << ' ' << yiq.y << ' ' << yiq.i << ' ' << yiq.q << ' ' << ctor(yiq.i, yiq.q) << ' ' << atan2deg(yiq.q, yiq.i) << endl;
				}
				angle[l] = atan2deg(q, i);

//				cerr << "angle of " << l << " is " << angle[l] << ' ' <<  endl; 
			}

			// XXX:  This still feels dodgy, but when we look at a 4-line sequence phase inversion
			// depends on whether the second or third line has different phase from first/4th 
			int phasecount = 0, tot = 0;
			for (int l = 20; l < (in_y - 4); l += 4, tot++) {
				if (fabs(angle[l + 1] - angle[l]) < 20) phasecount++; 
			}
			bool phase = phasecount > (tot / 2);
			//cerr << "phase " << phase << endl; 
			
			for (int l = firstline; l < (in_y - 2); l++) {
				double burstlev = 8; // Frame[f].rawbuffer[(l * in_x) + 1] / irescale;
				uint16_t *line_output = &output[(in_x * 3 * (l - firstline))];
				int o = 0;

				if (burstlev > 5) {
					if (aburstlev < 0) aburstlev = burstlev;	
					aburstlev = (aburstlev * .99) + (burstlev * .01);
				}
				if (f == f_debugline + lineoffset) cerr << "burst level " << burstlev << " mavg " << aburstlev << endl;

				double angleadj = 135 - angle[l];
	
				for (int h = 0; h < in_x; h++) {
					double adj2 = 0;

					double i = +(cbuf[l].p[h].i);
					double q = +(cbuf[l].p[h].q);

					double mag = ctor(i, q);

					double angle = atan2(q, i) + (((angleadj + adj2) / 180.0) * M_PIl);

					if (l == (f_debugline + lineoffset))
						cerr << "A " << h << ' ' << cbuf[l].p[h].i << ' ' << cbuf[l].p[h].q << ' ' ;

					cbuf[l].p[h].i = cos(angle) * mag;
					cbuf[l].p[h].q = sin(angle) * mag;

					if (l == (f_debugline + lineoffset)) 
						cerr << cbuf[l].p[h].i << ' ' << cbuf[l].p[h].q << endl ;
				}

				for (int h = 0; h < in_x; h++) {
					RGB r;
					YIQ yiq = cbuf[l].p[h + 0];
					YIQ yiqp = cbuf[l - 2].p[h + 0];
					
//					yiq.i = (yiqp.i + yiq.i) / 1;
//					yiq.q = (yiqp.q + yiq.q) / 1;

					yiq.i *= (10 / aburstlev);
					yiq.q *= (10 / aburstlev);
					
					yiqp.i *= (10 / aburstlev);
					yiqp.q *= (10 / aburstlev);
					
					double i = yiq.i, q = yiq.q;
					double ip = yiqp.i, qp = yiqp.q;
				
					int rotate = l % 4;
					bool flip = (rotate == 1) || (rotate == 2);
					if (phase) flip = !flip;

					//flip = 1;

					if (flip) {
						yiq.i = -q;
						yiq.q = -i;
					}

					if (f_showk) {
						yiq.y = ire_to_u16(Frame[f].combk[dim - 1][l][h + 82] * 100);
//						yiq.y = ire_to_u16(((double)h / 752.0) * 100);
						yiq.i = yiq.q = 0;
					}

					if (l == (f_debugline + lineoffset)) {
						cerr << "YIQ " << h << ' ' << l << ' ' << l % 4 << ' ' << angle[l] << ' ' << atan2deg(yiq.q, yiq.i) << ' ' << yiq.y << ' ' << yiq.i << ' ' << yiq.q << endl;
					}

					cline = l;
					r.conv(yiq); // , 135 - ang, l);
					
					if (l == (f_debugline + lineoffset)) {
//						cerr << "RGB " << r.r << ' ' << r.g << ' ' << r.b << endl ;
						r.r = r.g = r.b = 0;
					}
	
					line_output[o++] = (uint16_t)(r.r); 
					line_output[o++] = (uint16_t)(r.g); 
					line_output[o++] = (uint16_t)(r.b); 
				}
			}
		}

	public:
		Comb() {
			fieldcount = curline = linecount = -1;
			framecode = framecount = frames_out = 0;

			scount = 0;

			aburstlev = -1;

			f_oddframe = false;	
		
			f_hpy = new IIRFilter<25, 1>(f_nr);
			f_hpi = new IIRFilter<17, 1>(f_nrc);
			f_hpq = new IIRFilter<17, 1>(f_nrc);
			
			f_hpvy = new IIRFilter<25, 1>(f_nr);
			f_hpvi = new IIRFilter<17, 1>(f_nrc);
			f_hpvq = new IIRFilter<17, 1>(f_nrc);

			memset(output, 0, sizeof(output));
		}

		void WriteFrame(uint16_t *obuf, int owidth = 1052, int fnum = 0) {
			cerr << "WR" << fnum << endl;
			if (!f_writeimages) {
				if (!f_write8bit) {
					write(ofd, obuf, (owidth * linesout * 3) * 2);
				} else {
					uint8_t obuf8[owidth * linesout * 3];	

					for (int i = 0; i < (owidth * linesout * 3); i++) {
						obuf8[i] = obuf[i] >> 8;
					}
					write(ofd, obuf8, (owidth * linesout * 3));
				}		
			} else {
				char ofname[512];
				
				sprintf(ofname, "%s%d.rgb", image_base, fnum); 
				cerr << "W " << ofname << endl;
				ofd = open(ofname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IROTH);
				write(ofd, obuf, (owidth * linesout * 3) * 2);
				close(ofd);
			}

			if (f_oneframe) exit(0);
			frames_out++;
		}

		void AdjustY(int f, cline_t cbuf[in_y]) {
			int firstline = (linesout == in_y) ? 0 : lineoffset;
			// remove color data from baseband (Y)	
			for (int l = firstline; l < in_y; l++) {
				bool invertphase = (Frame[f].rawbuffer[l * in_x] == 16384);

				for (int h = 2; h < in_x; h++) {
					double comp;	
					int phase = h % 4;

					YIQ y = cbuf[l].p[h + 2];

					switch (phase) {
						case 0: comp = y.i; break;
						case 1: comp = -y.q; break;
						case 2: comp = -y.i; break;
						case 3: comp = y.q; break;
						default: break;
					}

					if (invertphase) comp = -comp;
					y.y += comp;

					cbuf[l].p[h + 0] = y;
				}
			}

		}

		// buffer: in_xxin_y uint16_t array
		void Process(uint16_t *buffer, int dim = 2)
		{
			int firstline = (linesout == in_y) ? 0 : lineoffset;
			int f = (dim == 3) ? 1 : 0;

			cerr << "P " << f << ' ' << dim << endl;

			memcpy(&Frame[2], &Frame[1], sizeof(frame_t));
			memcpy(&Frame[1], &Frame[0], sizeof(frame_t));
			memset(&Frame[0], 0, sizeof(frame_t));

			memcpy(Frame[0].rawbuffer, buffer, (in_x * in_y * 2));

			Split1D(0);
			if (dim >= 2) Split2D(0); 
			SplitIQ(0);
		
			// copy VBI	
			for (int l = 0; l < 24; l++) {
				uint16_t *line = &Frame[0].rawbuffer[l * in_x];	
					
				for (int h = 4; h < (in_x - 4); h++) {
					Frame[0].cbuf[l].p[h].y = line[h]; 
				}
			}
			SplitIQ(f);

			memcpy(tbuf, Frame[f].cbuf, sizeof(tbuf));	

			AdjustY(f, tbuf);
			if (f_colorlpf) FilterIQ(tbuf, f);
			DoYNR(f, tbuf);
			//DoCNR(f, tbuf);
			ToRGB(f, firstline, tbuf);
	
			PostProcess(f);
			framecount++;

			return;
		}
		
		int PostProcess(int fnum) {
			int fstart = -1;
			uint16_t *fbuf = Frame[fnum].rawbuffer;

			int out_x = f_wide ? in_x : 1052 - 78;
			int roffset = f_wide ? 0 : 78;

			if (!f_pulldown) {
				fstart = 0;
			} else if (f_oddframe) {
				for (int i = 1; i < linesout; i += 2) {
					memcpy(&obuf[out_x * 3 * i], &output[(in_x * 3 * i) + (roffset * 3)], out_x * 3 * 2); 
				}
				WriteFrame(obuf, out_x, framecode);
				f_oddframe = false;		
			}

			uint16_t flags = fbuf[7];

			cerr << "flags " << hex << flags << dec << endl;
//			if (flags & FRAME_INFO_CAV_ODD) fstart = 1;
			if (flags & FRAME_INFO_WHITE_ODD) fstart = 1;
			else if (flags & FRAME_INFO_WHITE_EVEN) fstart = 0;

			framecode = (fbuf[8] << 16) | fbuf[9];

			cerr << "FR " << framecount << ' ' << fstart << endl;
			if (!f_pulldown || (fstart == 0)) {
				for (int i = 0; i < linesout; i++) {
					memcpy(&obuf[out_x * 3 * i], &output[(in_x * 3 * i) + (roffset * 3)], out_x * 3 * 2); 
				}
				WriteFrame(obuf, out_x, framecode);
			} else if (fstart == 1) {
				for (int i = 0; i < linesout; i += 2) {
					memcpy(&obuf[out_x * 3 * i], &output[(in_x * 3 * i) + (roffset * 3)], out_x * 3 * 2); 
				}
				f_oddframe = true;
				cerr << "odd frame\n";
			}

			return 0;
		}
};
	
Comb comb;

void usage()
{
	cerr << "comb: " << endl;
	cerr << "-i [filename] : input filename (default: stdin)\n";
	cerr << "-o [filename] : output filename/base (default: stdout/frame)\n";
	cerr << "-d [dimensions] : Use 2D/3D comb filtering\n";
	cerr << "-B : B&W output\n";
	cerr << "-f : use separate file for each frame\n";
	cerr << "-p : use white flag/frame # for pulldown\n";	
	cerr << "-l [line] : debug selected line - extra prints for that line, and blacks it out\n";	
	cerr << "-h : this\n";	
}
	
unsigned short inbuf[in_x * in_y * 2];
const int bufsize = in_x * in_y * 2;

int main(int argc, char *argv[])
{
	int rv = 0, fd = 0;
	long long dlen = -1, tproc = 0;
	unsigned char *cinbuf = (unsigned char *)inbuf;
	int c;

	char out_filename[256] = "";

	cerr << std::setprecision(10);
	cerr << argc << endl;
	cerr << strncmp(argv[1], "-", 1) << endl;

	opterr = 0;
	
	while ((c = getopt(argc, argv, "WQLakN:tc:r:R:8OwvDd:Bb:I:w:i:o:fphn:l:")) != -1) {
		switch (c) {
			case 'W':
				f_wide = !f_wide;
				break;
			case 'L':
				f_colorlpf = !f_colorlpf;
				break;
			case 'Q':
				f_colorlpf_hq = !f_colorlpf_hq;
				break;
			case 'a':
				f_adaptive2d = !f_adaptive2d;
				break;
			case 'c':
				sscanf(optarg, "%lf", &p_3dcore);
				break; 
			case 'r':
				sscanf(optarg, "%lf", &p_3drange);
				break; 
			case 'R':
				sscanf(optarg, "%lf", &p_3d2drej);
				break; 
			case '8':
				f_write8bit = true;
				break;
			case 'd':
				sscanf(optarg, "%d", &dim);
				break;
			case 'D':
				f_debug2d = true;
				dim = 3;
				break;
			case 'O':
				f_oneframe = true;
				break;
			case 'v':
				// copy in VBI area (B&W)
				linesout = in_y;
				break;
			case 'B':
				// B&W mode
				f_bw = true;
				dim = 2;
				break;
			case 'b':
				sscanf(optarg, "%lf", &brightness);
				break;
			case 'I':
				sscanf(optarg, "%lf", &black_ire);
				break;
			case 'n':
				sscanf(optarg, "%lf", &nr_y);
				break;
			case 'N':
				sscanf(optarg, "%lf", &nr_c);
				break;
			case 'h':
				usage();
				return 0;
			case 'f':
				f_writeimages = true;	
				break;
			case 'p':
				f_pulldown = true;	
				break;
			case 'i':
				// set input file
				fd = open(optarg, O_RDONLY);
				break;
			case 'o':
				// set output file base name for image mode
				image_base = (char *)malloc(strlen(optarg) + 2);
				strncpy(image_base, optarg, strlen(optarg) + 1);
				break;
			case 'l':
				// black out a desired line
				sscanf(optarg, "%d", &f_debugline);
				break;
			case 't': // training mode - write images as well
				f_training = true;
				f_writeimages = true;	
				dim = 3;
				break;
			case 'k':
				f_showk = true;
				break;
			default:
				return -1;
		} 
	} 

	if (p_3dcore < 0) p_3dcore = 1.25;
	if (p_3drange < 0) p_3drange = 5.5;
	p_3dcore *= irescale;
	p_3drange *= irescale;

	p_2dcore = 0 * irescale;
	p_2drange = 10 * irescale;

	black_u16 = ire_to_u16(black_ire);

	nr_y *= irescale;
	nr_c *= irescale;

	if (!f_writeimages && strlen(out_filename)) {
		ofd = open(image_base, O_WRONLY | O_CREAT);
	}

	cout << std::setprecision(8);

	rv = read(fd, inbuf, bufsize);
	while ((rv > 0) && (rv < bufsize)) {
		int rv2 = read(fd, &cinbuf[rv], bufsize - rv);
		if (rv2 <= 0) exit(0);
		rv += rv2;
	}

	while (rv == bufsize && ((tproc < dlen) || (dlen < 0))) {
		comb.Process(inbuf, dim);
	
		rv = read(fd, inbuf, bufsize);
		while ((rv > 0) && (rv < bufsize)) {
			int rv2 = read(fd, &cinbuf[rv], bufsize - rv);
			if (rv2 <= 0) exit(0);
			rv += rv2;
		}
	}

	return 0;
}

