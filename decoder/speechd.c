/**
 * Speech Decoder - Pocketsphinx - Computer speech recognizer
 * @author Nick Blackwell https://people.ok.ubc.ca/nblackwe
 * 
 * @usage: this command line tool will process s16le pcm stream or wav files and attempt
 * decode the audio speech into text using the pocketsphinx lib and related language 
 * acoustic models
 *
 * compiling:
 * export LD_LIBRARY_PATH=/usr/local/lib
 * export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
 * 
 * gcc -o speechdecode speechd.c `pkg-config --cflags --libs pocketsphinx sphinxbase`
 *
 * run:
 * cat path-to-wav/file.wav | ./speechdecode
 *
 * or
 *
 * ./speechdecode path-to-wav/file.wav 
 *
 */



#include <sys/types.h>
#include <sys/time.h>
#include <math.h>

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <sphinxbase/cont_ad.h>
#include <pocketsphinx.h>

#include <stdio.h>

#include <time.h>





int main(int argc, char *argv[])
{
	ps_decoder_t *ps;
	cmd_ln_t *config;



	char cwd[1024];
	char arg[1024];
	char path[1024];
	memset(arg, '\0', sizeof(arg));
	strcpy(arg, argv[0]);

	if (getcwd(cwd, sizeof(cwd)) != NULL){

		int i=0, j=0;
		while(arg[i]!='\0'){
			if(arg[i]=='/')j=i;
			//printf("%c\n", arg[i]);
			i++;

		}
		arg[j]='\0';


		if(j<2){
			sprintf(path, "%s/", cwd, j);
		}else{
			sprintf(path, "%s/%s/", cwd, &arg[2], j);
		}
		//printf(path);

	}

	char hmm[1024];
	sprintf(hmm, "%smodels/hmm/en-us", path);
	//printf(hmm);

	char lm[1024];
	sprintf(lm, "%smodels/lm/cmusphinx-5.0-en-us.lm.dmp", path);
	//printf(lm);

	char dict[1024];
	sprintf(dict, "%smodels/dict/cmu07a.dic", path);
	//printf(dict);

	config = cmd_ln_init(NULL, ps_args(), 0,

			"-hmm", hmm,
			"-lm", lm,
			"-dict", dict,

			//"-mmap", "no",
			"-logfn", "errors.log",
			//"-dither","yes",

			NULL);



	if (config == NULL){
		perror("Failed to configure environment for pocketsphinx");
		return 1;
	}

	ps = ps_init(config);
	if (ps == NULL){
		perror("Failed to initialise pocketsphinx decoder");
		return 1;
	}

	if(argc==1){
		freopen(NULL, "rb", stdin);
		decodeinput(ps, stdin);
	}else{
		FILE *fh;

		fh = fopen(argv[1], "rb");
		if (fh == NULL) {
			perror("Failed to open goforward.raw");
			return 1;
		}
		fseek(fh, 0, SEEK_SET);

		decodeinput(ps, fh );
	}
	ps_free(ps);
	return 0;
}


int decodeinput(ps_decoder_t *ps, FILE *input)
{
	int rv;
	int size=1024*64; //approx 10 sec
	int bytes_per_sample=2;
	float sample_hertz=16000.0;

	printf("// Input 16bit Mono - Little Endian - 16000 samples per second\n",size/1024, (size/2.0)/16000.0);
	printf("// Read Buffer: %dKB - %0.2f Seconds (Max Utterance)\n",size/1024, (size/2.0)/16000.0);

	int16 buf[size];


	char waveheader[44];
	fread(waveheader, 1, 44, input);

	printf("// Discarding Header: 44-Bytes (.wav)\n", waveheader);

	size_t nsamp=0;
	size_t end_pos=nsamp;


	int j=0;

	int sampled=0;
	int processed=0;
	while(!feof(input)){

		char s[20];
		sprintf(s,"#%03d\n", j+1);
		//printf(s);

		rv = ps_start_utt(ps, s);

		float start_time=processed/sample_hertz;
		//nsamp will equal size if not first round and previous round used all 'size' of buffer
		if(nsamp==size){
			//if end is less than nsamp, then there is left over samples from last round.
			if(end_pos<nsamp){
				int leftover=0; int i;
				for(i=end_pos;i<nsamp;i++){
					buf[leftover++]=buf[i];
				}
				//printf("Adding %3d samples from previous round\n",leftover);
				rv = decode_bytes(ps, buf, leftover);
				processed+=leftover;
			}
		}



		nsamp = fread(buf, bytes_per_sample, size, input);




		sampled+=nsamp;


		//printf("# %04d:  %04d-Bytes,  Interval: %0.2f - %0.2f\n", j, nsamp*bytes_per_sample, start_time, end_time);
		//prints the sample as bytes
		//print_buffer_bytes(buf, nsamp);


		end_pos=nsamp;
		if(nsamp==size){
			//find a good spot to split the string.
			//set end to new value less than nsamp.
			end_pos-=64; //test. go back 64 samples...
			//printf("Backtracking 64 samples\n");

			end_pos=scrollback_scan(buf, nsamp);
			processed+=end_pos;

		}else{
			//last utterance...
			processed+=end_pos;
		}

		float end_time=processed/sample_hertz;


		if(end_pos>0){
			rv = decode_bytes(ps, buf, end_pos);
			rv = ps_end_utt(ps);
			if (rv < 0)
				return 1;

			char const *hyp, *uttid;
			int32 hyp_score;

			hyp = ps_get_hyp(ps, &hyp_score, &uttid);

			int32 itter_score;


			if (hyp != NULL){
				if(strcmp(hyp, "")!=0){
					printf("{\n");
					printf("   \"text\":\"%s\","
							" // the decoded text"
							"\n   \"speaker\":0,"
							" // the human voice id, id's are assigned to voices as speakers are detected"
							"\n   \"time_start\":%0.2f,"
							" // the start time for this utterance"
							"\n   \"time_len\":%0.2f,"
							" // the length of this utterance"
							"\n   \"score\":%d"
							" // pocketsphinx score"
							"\n", hyp, start_time, (end_time-start_time), hyp_score);

					int segments=0;
					if(0){
						printf(",\n   \"words\":[\n");
						ps_seg_t  *ittr=ps_seg_iter(ps, &itter_score);

						while(ittr!=NULL){

							char const *word = ps_seg_word(ittr);

							int32 out_ascr, out_lscr, out_lback;
							int32 prob=ps_seg_prob(ittr, &out_ascr, &out_lscr, &out_lback);


							int out_sf, out_ef;
							ps_seg_frames(ittr, &out_sf, &out_ef);

							if(segments>0)printf(", ");
							printf("      {\"word\":\"%s\", \"probability\":[%d, %d, %d, %d}, \"fs\":%d, \"fe\":%d}\n",word, prob, out_ascr, out_lscr, out_lback, out_sf, out_ef);
							ittr=ps_seg_next(ittr);
							segments++;

						}

						printf("   ], \n   ");
						printf("\"count\":%d", segments);
					}
					printf("\n}\r\n");

				}
			}
		}else{
			if(j==0){
				perror("no samples");
				return 1;
			}
		}


		j++;

	}

	int total_samples=(j-1)*size+nsamp;
	int total_bytes=bytes_per_sample*total_samples;
	printf("// Total: %4d-KBs Read  %0.2f Seconds \n", total_bytes/1024, (total_samples)/sample_hertz);
	//ie 89160 / 2 = 44580
	//total / 2 =(2 bytes per sample (16bit))


	fclose(input);

	return 0;

}

void print_buffer_bytes(int16 const *buf, size_t size){

	int i;
	for(i=0;i<size;i++){


		if(i%32==0)printf("\n%03x: ",i);
		if(i%8==0)printf("   ");
		if(buf[i]<0){
			printf("%04X ",((unsigned)buf[i]-0xFFFF0000));
		}else{
			printf("%04X ",buf[i]);
		}
	}

	printf("\n\n");


}

int decode_bytes(ps_decoder_t *ps,
               int16 const *data,
               size_t n_samples){
	return ps_process_raw(ps, data, n_samples, 0, 0);

}

int scrollback_scan(int16 const *buf, size_t samples_len){

	int frame_size=64;



	int pos;
	int middle=samples_len/2;
	int nframes=middle/frame_size + 1 ;

	float rmsbuf[nframes];

	float max[3]={0.0,0.0,0.0};
	float min[3]={999.0,999.0,999.0};

	int count=0;
	for(pos=samples_len-frame_size;pos>=middle;pos-=frame_size){
		int i=0;
		double rms=0;
		for(i=0;i<frame_size;i++){
			double j=buf[pos+1];
			j*=j;
			rms+=j;
		}

		rms/=frame_size;
		rms=sqrt(rms);

		if(count<nframes){
			rmsbuf[count]=rms;
			count++;
		}

		if(rms>max[0]){
			max[2]=max[1];
			max[1]=max[0];
			max[0]=rms;
		}else if(rms>max[1]){
			max[2]=max[1];
			max[1]=rms;
		}else if(rms>max[2]){
			max[2]=rms;
		}

		if(rms<min[0]){
			min[2]=min[1];
			min[1]=min[0];
			min[0]=rms;
		}else if(rms<min[1]){
			min[2]=min[1];
			min[1]=rms;
		}else if(rms<min[2]){
			min[2]=rms;
		}

	}

	float thresh_hold=0.05*((max[0]+max[1]+max[3])/3.0)-((min[0]+min[1]+min[3])/3.0);
	thresh_hold+=((min[0]+min[1]+min[3])/3.0);


	//printf("count: %d\n",count);
	int i;
	int k=0;
	int best_sample_frame=0;
	int longest=0;
	for(i=0;i<count;i++){
		if(rmsbuf[i]<=thresh_hold){
			k++;
			if(k>longest){
				longest=k;
				best_sample_frame=i;
			}
		}else{
			k=0;
		}
	}

	best_sample_frame-=(longest/2);

	printf("// Splitting ~ %0.2frms at - %0.2f (%d - samples)\n", thresh_hold, best_sample_frame*frame_size/16000.0, longest);
	return samples_len-(best_sample_frame*frame_size);

}



