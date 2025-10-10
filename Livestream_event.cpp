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
ReaderWriterQueue<Mat> disp_frame(30);

int last = 0, fps = 0;

// This class defines the properties, parameters, and the event handler itself. Take a
// moment to notice what parts of the class are mandatory, and what have been
// added for demonstration purposes. First, any class used to define image event handlers
// must inherit from ImageEventHandler. Second, the method signature of OnImageEvent()
// must also be consistent. Everything else - including the constructor,
// deconstructor, properties, body of OnImageEvent(), and other functions -
// is particular to the example.
class ImageEventHandlerImpl : public ImageEventHandler
{
public:
    // The constructor retrieves the serial number and initializes the image
    // counter to 0.
    ImageEventHandlerImpl(CameraPtr pCam)
    {
        // Retrieve device serial number
        INodeMap& nodeMap = pCam->GetTLDeviceNodeMap();

        m_deviceSerialNumber = "";
        CStringPtr ptrDeviceSerialNumber = nodeMap.GetNode("DeviceSerialNumber");
        if (IsReadable(ptrDeviceSerialNumber))
        {
            m_deviceSerialNumber = ptrDeviceSerialNumber->GetValue();
        }

        // Release reference to camera
        pCam = nullptr;

        //
        // Set default image processor color processing method
        //
        // *** NOTES ***
        // By default, if no specific color processing algorithm is set, the image
        // processor will default to NEAREST_NEIGHBOR method.
        //
        m_processor.SetColorProcessing(SPINNAKER_COLOR_PROCESSING_ALGORITHM_HQ_LINEAR);
    }

    ~ImageEventHandlerImpl()
    {
    }

    // This method defines an image event. In it, the image that triggered the
    // event is converted and saved before incrementing the count. Please see
    // Acquisition_CSharp example for more in-depth comments on the acquisition
    // of images.
    void OnImageEvent(ImagePtr pImage)
    {
        ImagePtr pResultImage;

        fps = pCam->GetNodeMap().GetNode("AcquisitionFrameRate")

            // Perform the deep copy
            pResultImage->DeepCopy(pImage);

        if (stream)
            q.enqueue(img);

        return;

    }
};

int ConfigureImageEvents(CameraPtr pCam, ImageEventHandlerImpl*& imageEventHandler)
{
    int result = 0;

    try
    {
        //
        // Create image event handler
        //
        // *** NOTES ***
        // The class has been constructed to accept a camera pointer in order
        // to allow the saving of images with the device serial number.
        //
        imageEventHandler = new ImageEventHandlerImpl(pCam);

        //
        // Register image event handler
        //
        // *** NOTES ***
        // Image event handlers are registered to cameras. If there are multiple
        // cameras, each camera must have the image event handlers registered to it
        // separately. Also, multiple image event handlers may be registered to a
        // single camera.
        //
        // *** LATER ***
        // Image event handlers must be unregistered manually. This must be done prior
        // to releasing the system and while the image event handlers are still in
        // scope.
        //
        pCam->RegisterEventHandler(*imageEventHandler);
    }
    catch (Spinnaker::Exception& e)
    {
        cout << "Error: " << e.what() << endl;
        result = -1;
    }

    return result;
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


       
        cout << endl << endl << "*** IMAGE ACQUISITION ***" << endl << endl;

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

        pCam->BeginAcquisition();

        cout << "Acquiring images..." << endl;


        
        #pragma omp parallel sections num_threads(4)
        {
            #pragma omp section
            {   
                ImagePtr pResultImage;
                Mat frame;

                while (true)
                {
                    try
                    {
                        q.try_dequeue(pResultImage);

                        // Convert to OpenCV Mat (assuming mono8, adjust type if needed)
                        unsigned int width = static_cast<int>(pResultImage->GetWidth());
                        unsigned int height = static_cast<int>(pResultImage->GetHeight());
                        unsigned int rowBytes = static_cast<int>(pResultImage->GetStride());
                        Mat tframe(height, width, CV_8UC1, pResultImage->GetData(), rowBytes);

                        frame = tframe.clone();

                        putText(frame, to_string(frameRate), Point((width - 50), 10), FONT_HERSHEY_COMPLEX, 0.4, Scalar(255, 255, 255));
                        putText(frame, to_string(frame.total() * frame.elemSize()), Point((width - 50), 20), FONT_HERSHEY_COMPLEX, 0.4, Scalar(255, 255, 255));

                        disp_frame.try_enqueue(frame.clone());

                        //try_enqueue is not functioning here.
                        //if (!disp_frame.try_enqueue(frame.clone())) {
                        //    std::cout << "Warning: disp_frame queue is full, frame not enqueued." << std::endl;
                        //}
                        if (!stream)
                            break;

                    }
                    catch (Spinnaker::Exception& e)
                    {
                        cout << "Error: " << e.what() << endl;
                        break;
                    }
                }

            }
            #pragma omp section
            {
                Mat frame;

                while (true)
                {
                    if (disp_frame.try_dequeue(frame))
                    {
                        // Show the frame
                        imshow("Camera", frame);

                        waitKey(1);


                        if (!stream)
                            break;

                    }
                }
            }
            #pragma omp section
            {
                while (true)
                {
                    if (GetAsyncKeyState(VK_ESCAPE))
                    {
                        stream = false;
                        break;
                    }
                }
            }

        }

    }
    catch (Spinnaker::Exception& e)
    {
        cout << "Error: " << e.what() << endl;
        return -1;
    }

    // End acquisition
    pCam->EndAcquisition();

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
