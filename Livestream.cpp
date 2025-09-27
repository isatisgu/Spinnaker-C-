#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#include <iostream>
#include <sstream>
#include "Acquisition.h"
#include "stdafx.h"




using namespace Spinnaker;
using namespace Spinnaker::GenApi;
using namespace Spinnaker::GenICam;
using namespace std;
using namespace cv;
using namespace moodycamel;

bool stream = true;
int imageWidth = 512, imageHeight = 512;
int imageLeft = 384, imageTop = 256;


ReaderWriterQueue<ImagePtr> q(30);
ReaderWriterQueue<Mat> disp_frame(1);

int last = 0, fps = 0;

int ConvertTimeToFPS(int ctime, int ltime)
{
    int dtime;

    if (ctime < ltime)
        dtime = ctime + (8000 - ltime);
    else
        dtime = ctime - ltime;

    if (dtime > 0)
        dtime = 8000 / dtime;
    else
        dtime = 0;

    return dtime;
}



int main(int /*argc*/, char** /*argv*/)
{
    // Since this application saves images in the current folder
    // we must ensure that we have permission to write to this folder.
    // If we do not have permission, fail right away.
    //FILE* tempFile = fopen_s("test.txt", "w+");
    //if (tempFile == nullptr)
    //{
    //    cout << "Failed to create file in current folder.  Please check "
    //            "permissions."
    //         << endl;
    //    cout << "Press Enter to exit..." << endl;
    //    getchar();
    //    return -1;
    //}
    //fclose(tempFile);
    //remove("test.txt");

    // Print application build information
    cout << "Application build date: " << __DATE__ << " " << __TIME__ << endl << endl;

    // Retrieve singleton reference to system object
    SystemPtr system = System::GetInstance();

    // Print out current library version
    const LibraryVersion spinnakerLibraryVersion = system->GetLibraryVersion();
    cout << "Spinnaker library version: " << spinnakerLibraryVersion.major << "." << spinnakerLibraryVersion.minor
        << "." << spinnakerLibraryVersion.type << "." << spinnakerLibraryVersion.build << endl
        << endl;

    // Retrieve list of cameras from the system
    CameraList camList = system->GetCameras();

    const unsigned int numCameras = camList.GetSize();

    cout << "Number of cameras detected: " << numCameras << endl << endl;

    // Finish if there are no cameras
    if (numCameras == 0)
    {
        // Clear camera list before releasing system
        camList.Clear();

        // Release system
        system->ReleaseInstance();

        cout << "Not enough cameras!" << endl;
        cout << "Done! Press Enter to exit..." << endl;
        getchar();

        return -1;
    }


    //
    // Create shared pointer to camera
    //
    // *** NOTES ***
    // The CameraPtr object is a shared pointer, and will generally clean itself
    // up upon exiting its scope. However, if a shared pointer is created in the
    // same scope that a system object is explicitly released (i.e. this scope),
    // the reference to the shared point must be broken manually.
    //
    // *** LATER ***
    // Shared pointers can be terminated manually by assigning them to nullptr.
    // This keeps releasing the system from throwing an exception.
    //
    CameraPtr pCam = nullptr;

    int result = 0;

    // Select camera
    pCam = camList.GetByIndex(0);
    if (!pCam)
    {
        cout << "No camera found at index 0!" << endl;
        camList.Clear();
        system->ReleaseInstance();
        return -1;
    }

    try
    {
        // Retrieve TL device nodemap and print device information
        INodeMap& nodeMapTLDevice = pCam->GetTLDeviceNodeMap();

        result = PrintDeviceInfo(nodeMapTLDevice);

        // Initialize camera
        pCam->Init();

        // Retrieve GenICam nodemap
        INodeMap& nodeMap = pCam->GetNodeMap();

        // Set image region
        CIntegerPtr width = nodeMap.GetNode("Width");
        CIntegerPtr height = nodeMap.GetNode("Height");
        CIntegerPtr offsetX = nodeMap.GetNode("OffsetX");
        CIntegerPtr offsetY = nodeMap.GetNode("OffsetY");
        if (IsAvailable(width) && IsWritable(width)) width->SetValue(imageWidth);
        if (IsAvailable(height) && IsWritable(height)) height->SetValue(imageHeight);
        if (IsAvailable(offsetX) && IsWritable(offsetX)) offsetX->SetValue(imageLeft);
        if (IsAvailable(offsetY) && IsWritable(offsetY)) offsetY->SetValue(imageTop);

        // Set frame rate (if available)
        CBooleanPtr acquisitionFrameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
        if (IsAvailable(acquisitionFrameRateEnable) && IsWritable(acquisitionFrameRateEnable))
            acquisitionFrameRateEnable->SetValue(true);

        CFloatPtr frameRate = nodeMap.GetNode("AcquisitionFrameRate");
        if (IsAvailable(frameRate) && IsWritable(frameRate))
            frameRate->SetValue(50.0);

        // Set exposure (shutter)
        CFloatPtr exposureTime = static_cast<CFloatPtr>(nodeMap.GetNode("ExposureTime"));
        if (IsAvailable(exposureTime) && IsWritable(exposureTime))
            exposureTime->SetValue(2300.0); // microseconds

        // Fix the error by using the correct dereferencing method for the INodeMap object.  
        // Set gain
        CFloatPtr gain = static_cast<CFloatPtr>(pCam->GetNodeMap().GetNode("Gain"));
        if (IsAvailable(gain) && IsWritable(gain))
            gain->SetValue(0.0);
  
        // Set stream mode
        result = result | SetStreamMode(pCam);

        
        // Acquire images
        int result = 0;


        try
        {
            cout << endl << endl << "*** IMAGE ACQUISITION ***" << endl << endl;
            // Get the current frame rate; acquisition frame rate recorded in hertz
            //
            // *** NOTES ***
            // The video frame rate can be set to anything; however, in order to
            // have videos play in real-time, the acquisition frame rate can be
            // retrieved from the camera.
            //
            CFloatPtr ptrAcquisitionFrameRate = nodeMap.GetNode("AcquisitionFrameRate");
            if (!IsReadable(ptrAcquisitionFrameRate))
            {
                cout << "Unable to retrieve frame rate. Aborting..." << endl << endl;
                return -1;
            }

            float frameRateToSet = static_cast<float>(ptrAcquisitionFrameRate->GetValue());

            cout << "Frame rate to be set to " << frameRateToSet << "..." << endl;
            


            //
            // Set acquisition mode to continuous
            //
            // *** NOTES ***
            // Because the example acquires and saves 10 images, setting acquisition
            // mode to continuous lets the example finish. If set to single frame
            // or multiframe (at a lower number of images), the example would just
            // hang. This would happen because the example has been written to
            // acquire 10 images while the camera would have been programmed to
            // retrieve less than that.
            //
            // Setting the value of an enumeration node is slightly more complicated
            // than other node types. Two nodes must be retrieved: first, the
            // enumeration node is retrieved from the nodemap; and second, the entry
            // node is retrieved from the enumeration node. The integer value of the
            // entry node is then set as the new value of the enumeration node.
            //
            // Notice that both the enumeration and the entry nodes are checked for
            // availability and readability/writability. Enumeration nodes are
            // generally readable and writable whereas their entry nodes are only
            // ever readable.
            //
            // Retrieve enumeration node from nodemap
            CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
            if (!IsReadable(ptrAcquisitionMode) || !IsWritable(ptrAcquisitionMode))
            {
                cout << "Unable to set acquisition mode to continuous (enum retrieval). Aborting..." << endl << endl;
                return -1;
            }

            // Retrieve entry node from enumeration node
            CEnumEntryPtr ptrAcquisitionModeContinuous = ptrAcquisitionMode->GetEntryByName("Continuous");
            if (!IsReadable(ptrAcquisitionModeContinuous))
            {
                cout << "Unable to get or set acquisition mode to continuous (entry retrieval). Aborting..." << endl
                    << endl;
                return -1;
            }

            // Retrieve integer value from entry node
            const int64_t acquisitionModeContinuous = ptrAcquisitionModeContinuous->GetValue();

            // Set integer value from entry node as new value of enumeration node
            ptrAcquisitionMode->SetIntValue(acquisitionModeContinuous);

            cout << "Acquisition mode set to continuous..." << endl;



            //
            // Begin acquiring images
            //
            // *** NOTES ***
            // What happens when the camera begins acquiring images depends on the
            // acquisition mode. Single frame captures only a single image, multi
            // frame captures a set number of images, and continuous captures a
            // continuous stream of images. Because the example calls for the
            // retrieval of 10 images, continuous mode has been set.
            //
            // *** LATER ***
            // Image acquisition must be ended when no more images are needed.
            //
            pCam->BeginAcquisition();

            cout << "Acquiring images..." << endl;

            //
            // Retrieve device serial number for filename
            //
            // *** NOTES ***
            // The device serial number is retrieved in order to keep cameras from
            // overwriting one another. Grabbing image IDs could also accomplish
            // this.
            //
            gcstring deviceSerialNumber("");
            CStringPtr ptrStringSerial = nodeMapTLDevice.GetNode("DeviceSerialNumber");
            if (IsReadable(ptrStringSerial))
            {
                deviceSerialNumber = ptrStringSerial->GetValue();

                cout << "Device serial number retrieved as " << deviceSerialNumber << "..." << endl;
            }
            cout << endl;

            // Create ImageProcessor instance for post processing images
            //
            ImageProcessor processor;

            //
            // Set default image processor color processing method
            //
            // *** NOTES ***
            // By default, if no specific color processing algorithm is set, the image
            // processor will default to NEAREST_NEIGHBOR method.
            //
            processor.SetColorProcessing(SPINNAKER_COLOR_PROCESSING_ALGORITHM_HQ_LINEAR);

    //        try
    //        {
    //            //
    //            // Retrieve next received image
    //            //
    //            // *** NOTES ***
    //            // Capturing an image houses images on the camera buffer. Trying
    //            // to capture an image that does not exist will hang the camera.
    //            //
    //            // *** LATER ***
    //            // Once an image from the buffer is saved and/or no longer
    //            // needed, the image must be released in order to keep the
    //            // buffer from filling up.
    //            //

    //            ImagePtr pResultImage = pCam->GetNextImage(1000);

    //            //if (pResultImage)
    //            //{
    //            //    std::cout << "pResultImage is valid." << std::endl;
    //            //    std::cout << "Width: " << pResultImage->GetWidth() << std::endl;
    //            //    std::cout << "Height: " << pResultImage->GetHeight() << std::endl;
    //            //    std::cout << "Status: " << Image::GetImageStatusDescription(pResultImage->GetImageStatus()) << std::endl;
    //            //}
    //            //else
    //            //{
    //            //    std::cout << "pResultImage is null." << std::endl;
    //            //}

    //            //
    //            // Ensure image completion
    //            //
    //            // *** NOTES ***
    //            // Images can easily be checked for completion. This should be
    //            // done whenever a complete image is expected or required.
    //            // Further, check image status for a little more insight into
    //            // why an image is incomplete.
    //            //
    //            if (pResultImage->IsIncomplete())
    //            {
    //                // Retrieve and print the image status description
    //                cout << "Image incomplete: " << Image::GetImageStatusDescription(pResultImage->GetImageStatus())
    //                    << "..." << endl
    //                    << endl;
    //            }
    //            else
    //            {
    //                //
    //                // Print image information; height and width recorded in pixels
    //                //
    //                // *** NOTES ***
    //                // Images have quite a bit of available metadata including
    //                // things such as CRC, image status, and offset values, to
    //                // name a few.
    //                //
    //                // To deep copy a Spinnaker::ImagePtr (pResultImage), use the Image::DeepCopy method.
    //                // Example:

				//	if (stream)
    //                    if (pResultImage)
    //                    {
    //                        q.enqueue(pResultImage);
    //                    }
    //                    else
    //                    {
    //                        std::cout << "Warning: pImage is null, not enqueuing." << std::endl;
    //                    }

    //                //
    //                // Release image
    //                //
    //                // *** NOTES ***
    //                // Images retrieved directly from the camera (i.e. non-converted
    //                // images) need to be released in order to keep from filling the
    //                // buffer.
    //                //
    //                pResultImage->Release();

    //                cout << endl;
    //        
    //            }
    //            #pragma omp parallel sections num_threads(4)
    //            {
    //                #pragma omp section
    //                while (true)
    //                {
    //                    if (q.try_dequeue(img))
    //                    {
    //                        Mat frame;

    //                        unsigned int width = static_cast<int>(pResultImage->GetWidth());
    //                        unsigned int height = static_cast<int>(pResultImage->GetHeight());
    //                        unsigned int rowBytes = static_cast<int>(pResultImage->GetStride()); // Spinnaker ImagePtr::GetStride() gives row bytes
    //                        Mat tframe = Mat(height, width, CV_8UC1, pResultImage->GetData(), rowBytes); // Spinnaker ImagePtr::GetHeight(), GetWidth(), GetData()

    //                        frame = tframe.clone();

    //                        disp_frame.try_enqueue(frame.clone());

    //                    }
				//		if (!stream)
				//			break;
				//	}
				//}
    //            #pragma omp section
    //            {

    //                Mat frame;

    //                while (true)
    //                {

    //                    if (disp_frame.try_dequeue(frame))
    //                        imshow("Camera", frame);

    //                    waitKey(1);

    //                    if (!stream)
    //                        break;
    //                }
    //            }

    //        #pragma omp section
    //            {
    //                while (true)
    //                {
    //                    if (GetAsyncKeyState(VK_ESCAPE))
    //                    {
    //                        stream = false;
    //                        break;

    //                    }
    //                }




    //            }
    //        }
            // Start acquisition
            cout << "Press ESC to exit live view..." << endl;

            while (stream)
            {
                try
                {
                    // Acquire next image
                    ImagePtr pResultImage = pCam->GetNextImage(1000);

                    if (!pResultImage->IsIncomplete())
                    {
                        // Convert to OpenCV Mat (assuming mono8, adjust type if needed)
                        unsigned int width = static_cast<int>(pResultImage->GetWidth());
                        unsigned int height = static_cast<int>(pResultImage->GetHeight());
                        unsigned int rowBytes = static_cast<int>(pResultImage->GetStride());
                        Mat frame(height, width, CV_8UC1, pResultImage->GetData(), rowBytes);

                        // Show the frame
                        imshow("Camera", frame);

                        // Wait for 1 ms and check for ESC key
                        if (waitKey(1) == 27) // 27 is ESC
                            stream = false;
                    }
                    pResultImage->Release();
                }
                catch (Spinnaker::Exception& e)
                {
                    cout << "Error: " << e.what() << endl;
                    break;
                }
            }
           
            // End acquisition
            pCam->EndAcquisition();


            //catch (Spinnaker::Exception& e)
            //{
            //    cout << "Error: " << e.what() << endl;
            //    result = -1;
            //}

            ////
            // // End acquisition
            // //
            // // *** NOTES ***
            // // Ending acquisition appropriately helps ensure that devices clean up
            // // properly and do not need to be power-cycled to maintain integrity.
            // //
            //pCam->EndAcquisition();


        }
        catch (Spinnaker::Exception& e)
        {
            cout << "Error: " << e.what() << endl;
            return -1;
        }

        //#ifdef _DEBUG
        //        // Reset heartbeat for GEV camera
        //        result = result | ResetGVCPHeartbeat(pCam);
        //#endif
        //
        //        // Deinitialize camera
        //        pCam->DeInit();
    }
    catch (Spinnaker::Exception& e)
    {
        cout << "Error: " << e.what() << endl;
        result = -1;
    }
    
    //
    // Release reference to the camera
    //
    // *** NOTES ***
    // Had the CameraPtr object been created within the for-loop, it would not
    // be necessary to manually break the reference because the shared pointer
    // would have automatically cleaned itself up upon exiting the loop.
    //
    pCam = nullptr;

    // Clear camera list before releasing system
    camList.Clear();

    // Release system
    system->ReleaseInstance();

    cout << endl << "Done! Press Enter to exit..." << endl;
    getchar();


    return result;
}
