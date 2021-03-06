////////////////////////////////////////////////////////////////////
//This program references find_obj.cpp in  OpenCV sample directory.
///////////////////////////////////////////////////////////////////

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <opencv2/opencv.hpp>

using namespace std;
#define USE_FLANN

//Compare SURF Descriptors.
//Copy from find_obj.cpp
double compareSURFDescriptors( const float* d1, const float* d2, double best, int length )
{
    double total_cost = 0;
    assert( length % 4 == 0 );
    for( int i = 0; i < length; i += 4 )
    {
        double t0 = d1[i] - d2[i];
        double t1 = d1[i+1] - d2[i+1];
        double t2 = d1[i+2] - d2[i+2];
        double t3 = d1[i+3] - d2[i+3];
        total_cost += t0*t0 + t1*t1 + t2*t2 + t3*t3;
        if( total_cost > best )
            break;
    }
    return total_cost;
}

int naiveNearestNeighbor( const float* vec, int laplacian,    const CvSeq* model_keypoints,   const CvSeq* model_descriptors )
{
    int length = (int)(model_descriptors->elem_size/sizeof(float));
    int i, neighbor = -1;
    double d, dist1 = 1e6, dist2 = 1e6;
    CvSeqReader reader, kreader;
    cvStartReadSeq( model_keypoints, &kreader, 0 );
    cvStartReadSeq( model_descriptors, &reader, 0 );

    for( i = 0; i < model_descriptors->total; i++ )
    {
        const CvSURFPoint* kp = (const CvSURFPoint*)kreader.ptr;
        const float* mvec = (const float*)reader.ptr;
    	CV_NEXT_SEQ_ELEM( kreader.seq->elem_size, kreader );
        CV_NEXT_SEQ_ELEM( reader.seq->elem_size, reader );
        if( laplacian != kp->laplacian )
            continue;
        d = compareSURFDescriptors( vec, mvec, dist2, length );
        if( d < dist1 )
        {
            dist2 = dist1;
            dist1 = d;
            neighbor = i;
        }
        else if ( d < dist2 )
            dist2 = d;
    }
    if ( dist1 < 0.6*dist2 )
        return neighbor;
    return -1;
}

void findPairs( const CvSeq* objectKeypoints, const CvSeq* objectDescriptors,  const CvSeq* imageKeypoints, const CvSeq* imageDescriptors, vector<int>& ptpairs )
{
    int i;
    CvSeqReader reader, kreader;
    cvStartReadSeq( objectKeypoints, &kreader );
    cvStartReadSeq( objectDescriptors, &reader );
    ptpairs.clear();

    for( i = 0; i < objectDescriptors->total; i++ )
    {
        const CvSURFPoint* kp = (const CvSURFPoint*)kreader.ptr;
        const float* descriptor = (const float*)reader.ptr;
        CV_NEXT_SEQ_ELEM( kreader.seq->elem_size, kreader );
        CV_NEXT_SEQ_ELEM( reader.seq->elem_size, reader );
        int nearest_neighbor = naiveNearestNeighbor( descriptor, kp->laplacian, imageKeypoints, imageDescriptors );
        if( nearest_neighbor >= 0 )
        {
            ptpairs.push_back(i);
            ptpairs.push_back(nearest_neighbor);
        }
    }
}


void flannFindPairs( const CvSeq*, const CvSeq* objectDescriptors,     const CvSeq*, const CvSeq* imageDescriptors, vector<int>& ptpairs )
{
	int length = (int)(objectDescriptors->elem_size/sizeof(float));

    cv::Mat m_object(objectDescriptors->total, length, CV_32F);
	cv::Mat m_image(imageDescriptors->total, length, CV_32F);


	// copy descriptors
    CvSeqReader obj_reader;
	float* obj_ptr = m_object.ptr<float>(0);
    cvStartReadSeq( objectDescriptors, &obj_reader );
    for(int i = 0; i < objectDescriptors->total; i++ )
    {
        const float* descriptor = (const float*)obj_reader.ptr;
        CV_NEXT_SEQ_ELEM( obj_reader.seq->elem_size, obj_reader );
        memcpy(obj_ptr, descriptor, length*sizeof(float));
        obj_ptr += length;
    }
    CvSeqReader img_reader;
	float* img_ptr = m_image.ptr<float>(0);
    cvStartReadSeq( imageDescriptors, &img_reader );
    for(int i = 0; i < imageDescriptors->total; i++ )
    {
        const float* descriptor = (const float*)img_reader.ptr;
        CV_NEXT_SEQ_ELEM( img_reader.seq->elem_size, img_reader );
        memcpy(img_ptr, descriptor, length*sizeof(float));
        img_ptr += length;
    }

    // find nearest neighbors using FLANN
    cv::Mat m_indices(objectDescriptors->total, 2, CV_32S);
    cv::Mat m_dists(objectDescriptors->total, 2, CV_32F);
    cv::flann::Index flann_index(m_image, cv::flann::KDTreeIndexParams(4));  // using 4 randomized kdtrees
    flann_index.knnSearch(m_object, m_indices, m_dists, 2, cv::flann::SearchParams(64) ); // maximum number of leafs checked

    int* indices_ptr = m_indices.ptr<int>(0);
    float* dists_ptr = m_dists.ptr<float>(0);
    for (int i=0;i<m_indices.rows;++i) {
    	if (dists_ptr[2*i]<0.6*dists_ptr[2*i+1]) {
    		ptpairs.push_back(i);
    		ptpairs.push_back(indices_ptr[2*i]);
    	}
    }
}


int main(int argc, char** argv)
{
	char* goalImage = argc == 2 ? argv[1] : "3DS.png";
 	cvNamedWindow("piece", 1);
	cvNamedWindow("GoalImage", 1);
	IplImage* grayGoalImage = cvLoadImage( goalImage, CV_LOAD_IMAGE_GRAYSCALE );
	IplImage* ColorGoalImage= cvLoadImage( goalImage, 1 );
	CvMemStorage* storage = cvCreateMemStorage(0);

	CvSeq *objectKeypoints = 0, *objectDescriptors = 0;
	CvSeq *imageKeypoints = 0, *imageDescriptors = 0;
	int i;
	CvSURFParams params = cvSURFParams(500, 1);

	double tt = (double)cvGetTickCount();
    
	
	//Extract SURF Descriptors from GOAL image.
	cvExtractSURF( grayGoalImage, 0, &imageKeypoints, &imageDescriptors, storage, params );	
	printf("Image Descriptors: %d\n", imageDescriptors->total);
	tt = (double)cvGetTickCount() - tt;
	printf( "Extraction time = %gms\n", tt/(cvGetTickFrequency()*1000.));
    
	
	//Create copy image for show correspond points.
	IplImage* correspond = cvCloneImage(grayGoalImage);

	vector<int> ptpairs;
	
	CvCapture* capture=cvCreateCameraCapture(0);
	if(capture==NULL) exit(-1);
	
	IplImage* frame;		//original capture image
	IplImage* cameraImage;		//copy of capture image
	IplImage* graycameraImage;//gray-scaled capture image
	frame=cvQueryFrame(capture);
	cameraImage=cvCloneImage(frame);
	graycameraImage=cvCreateImage(cvGetSize(cameraImage),IPL_DEPTH_8U,1);


	IplImage *tempGoalImage=cvCloneImage(ColorGoalImage);


	while(true)
	{
		//Key assign
		// 'c'	Capture pieace image from WebCam
		// 'q' Exit program
		ptpairs.clear();
		frame=cvQueryFrame(capture);
		cvCopy(frame,cameraImage);
		cvShowImage( "piece", cameraImage );
		char c=cvWaitKey(30);
		if('q'==c) break;
		
		if('c'==c) 
		{
			//Extrac SURF Descriptors from Captured image,
			//and find pairs between descriptors of goal image and captured image.

			tt = (double)cvGetTickCount();	
			cvCopy(grayGoalImage,correspond);
			cvCvtColor(cameraImage,graycameraImage,CV_BGR2GRAY);
			
			//Create copy of goal image descriptor sequence
			CvMemStorage* tempstorage = cvCreateMemStorage(0);
			CvMemStorage* tempstorage2 = cvCreateMemStorage(0);
			CvMemStorage* objStorage=cvCreateMemStorage(0);
			CvSeq *tempimageKeypoints = cvCloneSeq(imageKeypoints,tempstorage);
			CvSeq *tempimageDescriptors = cvCloneSeq(imageDescriptors,tempstorage2);
	
			//Extract SURF Descriptors from captured image(image must gray scale.)
			cvExtractSURF( graycameraImage, 0, &objectKeypoints, &objectDescriptors, objStorage, params );
			if(objectDescriptors->total>0)
			{
				//flannFindPairs( objectKeypoints, objectDescriptors, tempimageKeypoints, tempimageDescriptors, ptpairs );
				findPairs( objectKeypoints, objectDescriptors, tempimageKeypoints, tempimageDescriptors, ptpairs);

				cvZero(tempGoalImage);
				cvCopy(ColorGoalImage,tempGoalImage);
				
				//Draw circle on correspond points.
				for( i = 0; i < (int)ptpairs.size(); i += 2 )
				{
					CvSURFPoint* r1 = (CvSURFPoint*)cvGetSeqElem( objectKeypoints, ptpairs[i] );
					cvCircle( cameraImage, cvPointFrom32f(r1->pt),5,CV_RGB(0,255,255),2);
					CvSURFPoint* r2 = (CvSURFPoint*)cvGetSeqElem( tempimageKeypoints, ptpairs[i+1] );
					cvCircle( tempGoalImage, cvPointFrom32f(r2->pt),5,CV_RGB(0,255,255),2);	
				}
				tt = (double)cvGetTickCount() - tt;
				tt= tt/(cvGetTickFrequency()*1000);
				printf("Object Descriptors: %d   process time(ms):%3.2f\n", objectDescriptors->total,tt);
				cvShowImage( "piece", cameraImage );
				cvShowImage("GoalImage",tempGoalImage);
				cvWaitKey(3000);

				//Clean up
				cvClearSeq(objectKeypoints);
				cvClearSeq(objectDescriptors);
				cvClearSeq(tempimageKeypoints); 
				cvClearSeq(tempimageDescriptors);
				cvReleaseMemStorage(&tempstorage);
				cvReleaseMemStorage(&tempstorage2);
				cvReleaseMemStorage(&objStorage);
			}
		}		
	}

	cvDestroyWindow("GoalImage");
	cvDestroyWindow("piece");
	cvReleaseImage(&grayGoalImage);
	cvReleaseImage(&ColorGoalImage);
	cvReleaseImage(&correspond);
	cvReleaseImage(&cameraImage);
	cvReleaseImage(&graycameraImage);
	 cvReleaseCapture(&capture);
	return 0;
}
