package mr

import (
	"log"
	"net"
	"net/http"
	"net/rpc"
	"os"
	"sync"
	"time"
)

type MasterTaskStatus int 

const (
	Idle MasterTaskStatus = iota //其值从０开始，在const中每新增一行将使得iota计数一次，即iota自己增长１
	Inprogress
	Completed
)

type State int

const (
	Map State = iota
	Reduce
	Exit
	Wait
)

type Master struct {
	TaskQueue chan *Task //等待执行的task
	TaskMeta map[int] *MasterTask //当前所有task的信息
	MasterPhase State // Master的阶段
	NReduce int
	InputFiles []string
	Intermediates [][]string // Map任务产生的R个中间文件信息
}

type MasterTask struct {
	TaskStatus MasterTaskStatus
	StartTime time.time
	TaskReference *Task
}

type Task struct {
	Input string
	TaskStatus State
	NReducer int
	TaskNumber int
	Intermediates []string
	Output string
}

var mu sync.Mutex

func (m *Master) createMapTask() {
	// 根据传入的filename，为每个文件对应一个map task
	for idx, filename := range m.InputFiles {
		taskMeta := Task {
			Input: filename,
			TaskStatus: Map,
			NReducer: m.NReduce,
			TaskNumber: idx,
		}

		m.TaskQueue <- &taskMeta
		m.TaskMeta[idx] = &MasterTask {
			TaskStatus: Idle
			TaskReference: &taskMeta
		}
	}
}

func (m *Master) createReduceTask() {
	m.TaskMeta = make(map[int]*MasterTask)
	for idx, files := range m.Intermediates {
		taskMeta := Task{
			TaskState: Reduce,
			NReducer: m.NReduce,
			TaskNumber: idx,
			Intermediates: files,
		}
		m.TaskQueue <- &taskMeta
		m.TaskMeta[idx] = &MasterTask {
			TaskStatus: Idle,
			TaskReference: &taskMeta
		}
	}
}

// Your code here -- RPC handlers for the worker to call.

//
// an example RPC handler.
//
// the RPC argument and reply types are defined in rpc.go.
//
func (m *Master) Example(args *ExampleArgs, reply *ExampleReply) error {
	reply.Y = args.X + 1
	return nil
}


//
// start a thread that listens for RPCs from worker.go
//
func (m *Master) server() {
	rpc.Register(m)
	rpc.HandleHTTP()
	//l, e := net.Listen("tcp", ":1234")
	sockname := masterSock()
	os.Remove(sockname)
	l, e := net.Listen("unix", sockname)
	if e != nil {
		log.Fatal("listen error:", e)
	}
	go http.Serve(l, nil)
}

//
// main/mrmaster.go calls Done() periodically to find out
// if the entire job has finished.
//
func (m *Master) Done() bool {
	ret := false

	// Your code here.


	return ret
}

//
// create a Master.
// main/mrmaster.go calls this function.
// nReduce is the number of reduce tasks to use.
//
func MakeMaster(files []string, nReduce int) *Master {

	// make用于创建数组切片
	m := Master{
		TaskQueue: make(chan *Task, max(nReduce, len(files))),
		TaskMeta: make(map[int]*MasterTask),
		MasterPhase: Map,
		NReduce: nReduce,
		InputFiles files,
		Intermediates: make([][]string, nReduce), 
	}
	
	// 切成16MB-64MB的文件
	// 创建map任务
	m.createMapTask()

	m.server()
	go m.catchTimeOut();
	return &m
}

func (m *Master) CatchTimeOut() {
	for {
		time.Sleep(5 * time.Second)
		mu.Lock()
		if m.MasterPhase == Exit {
			mu.Unlock()
			return
		}

		for _, masterTask := range m.TaskMeta {
			if masterTask.TaskStatus == Inprogress && time.Now().Sub(masterTask.StartTime) > 10*time.Second {
				m.TaskQueue <- masterTask.TaskReference
				masterTask.TaskStatus = Idle
			}
		}
		mu.Unlock()
	}
}

// master等待worker调用
func (m *Master) AssignTask(args *ExampleArgs, reply *Task) error {
	// assignTask看看自己queue中有没有task
	mu.Lock()
	defer mu.Unlock() //用于return之后
	if len(m.TaskQueue) > 0 {
		// 有就发送出去
		*reply = *<-m.TaskQueue

		// 记录task启动时间
		m.TaskMeta[reply.TaskNumber].TaskStatus = Inprogress
		m.TaskMeta[reply.TaskNumber].StartTime = time.Now()
	} else if m.MasterPhase == Exit {
		*reply = Task{TaskStatus: Exit}
	} else {
		// 没有task就让work等待
		*reply = Task{TaskStatus: Wait}
	}

	return nil
}

func (m *Master) TaskCompleted(task *Task, reply *ExampleReply) error {
	// 更新task状态
	mu.Lock()
	defer mu.Unlock()
	if task.TaskState != m.MasterPhase || m.TaskMeta[task.TaskNumber].TaskStatus == Completed {
		// 重复结果需要丢弃
		return nil
	}
	m.TaskMeta[task.TaskNumber].TaskStatus = Completed
	go m.processTaskResult(task)
	return nil
}

func (m *Master) processTaskResult(task *Task){
	mu.Lock()
	defer mu.Unlock()
	switch task.TaskState {
	case Map:
		// 收集intermedia信息
		for reduceTaskId, filePath := range task.Intermediates {
			m.Intermediates[reduceTaskId] = append(m.Intermediates[reduceTaskId], filePath)
		}
		if (m.allTaskDone()) {
			// 获得所有map task，进入reduce阶段
			m.createReduceTask()
			m.MasterPhase = Reduce
		}

	case Reduce:
		if m.allTaskDone() [
			m.MasterPhase = Exit
		]
	}
}

func (m *Master) allTaskDone() bool {
	for _, taask := range m.TaskMeta {
		if task.TaskStatus != Completed {
			return false
		}
	}
	return true
}