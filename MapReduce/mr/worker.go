package mr

import (
	"encoding/json"
	"fmt"
	"hash/fnv"
	"io/ioutil"
	"log"
	"net/rpc"
	"os"
	"path/filepath"
	"sort"
	"time"
)


//
// Map functions return a slice of KeyValue.
//
type KeyValue struct {
	Key   string
	Value string
}

// for sorting by key
type ByKey []KeyValue

// for sorting by key
func (a ByKey) Len() int {return len(a)}
func (a ByKey) Swap(i, j int) {a[i], a[j] = a[j], a[i]}
func (a ByKey) Less(i, j, int) {return a[i].Key < a[j].Key}

//
// use ihash(key) % NReduce to choose the reduce
// task number for each KeyValue emitted by Map.
//
func ihash(key string) int {
	h := fnv.New32a()
	h.Write([]byte(key))
	return int(h.Sum32() & 0x7fffffff)
}


//
// main/mrworker.go calls this function.
//
func Worker(mapf func(string, string) []KeyValue,
	reducef func(string, []string) string) {

		// 启动worker
		for {
			// worker从master中获取任务
			task := getTask()

			// 拿到task之后根据，task的state判断是mapper还是reducer
			switch task.TaskStatus {
			case Map:
				mapper(&task, mapf)
			case Reduce:
				reducer(&task, reducef)
			case Wait:
				time.Sleep(5 * time.Second)
			case Exit:
				return
			}
		}

}

//
// example function to show how to make an RPC call to the master.
//
// the RPC argument and reply types are defined in rpc.go.
//
func CallExample() {

	// declare an argument structure.
	args := ExampleArgs{}

	// fill in the argument(s).
	args.X = 99

	// declare a reply structure.
	reply := ExampleReply{}

	// send the RPC request, wait for the reply.
	call("Master.Example", &args, &reply)

	// reply.Y should be 100.
	fmt.Printf("reply.Y %v\n", reply.Y)
}

//
// send an RPC request to the master, wait for the response.
// usually returns true.
// returns false if something goes wrong.
//
func call(rpcname string, args interface{}, reply interface{}) bool {
	// c, err := rpc.DialHTTP("tcp", "127.0.0.1"+":1234")
	sockname := masterSock()
	c, err := rpc.DialHTTP("unix", sockname)
	if err != nil {
		log.Fatal("dialing:", err)
	}
	defer c.Close()

	err = c.Call(rpcname, args, reply)
	if err == nil {
		return true
	}

	fmt.Println(err)
	return false
}


func getTask() Task {
	// 从master中获取任务
	args := ExampleArgs{}
	reply := Task{}
	call("Master.AssignTask", &args, &reply)
	return reply
}

func mapper(task *Task, mapf func(string, string) []keyValue) {
	// 从文件名读取context
	content, err := ioutil.ReadFile(task.Input)
	if err != nil {
		log.Fatal("Failed to read file:" + task.Input, err)
	} 

	// content交给mapf，缓存结果
	intermediates := mapf(task.Input, string(content))

	// 缓存后的结果会写到本地磁盘，并切分R份
	buffer := make([][]KeyValue, task.NReduce)
	for _, intermediate := range intermediates {
		slot := ihash(intermediate.Key) % task.NReducer
		buffer[slot] = append(buffer[slot], intermediate)
	}

	mapOutput := make([]string, 0)
	for i:= 0; i < task.NReducer; i++ {
		mapOutput = append(mapOutput, WriteToLocalFile(task.TaskNumber, i, &buffer[i]))
	}

	// R个文件发送给master
	task.Intermediates = mapOutput
	TaskCompleted(task)
 }

 func reducer(task *Task, reducef func(string, []string) string) {
	 // 从filename读取intermediate的KeyValue
	 intermediate := *readFromLocalFile(task.Intermediates)

	 // 根据kv排序
	 sort.Sort(ByKey(intermediate))

	 dir, _ := os.Getwd()
	 tempFile, err := ioutil.TempFile(dir, "mr-tmp-*")
	 if err != nil {
		 log.Fatal("Filed to create temp file", err)
	 }

	 i := 0
	 for i < len(intermediate) {
		 // 将相同的key放在一起进行合并
		 j := i + 1
		 for j < len(intermediate) && intermediate[j].Key == intermediate[i].Key {
			 j ++;
		 }
		 values := []string{}
		 for k := i; k < j; k ++ {
			 values = append(values, intermediate[k].Value)
		 }
		 // 交给reducef，拿到结果
		 output := reducef(intermediate[i].Key, values)
		 // 写到对应的output文件
		 fmt.Fprintf(tempFile, "%v %v\n", intermediate[i].Key, output)
		 i = j
	 }
	 tempFile.Close()
	 oname := fmt.Sprintf("mr-out-%d", task.TaskNumber)
	 os.Rename(tempFile.Name(), oname)
	 task.Output = oname
	 TaskCompleted(task)
 }

 func TaskCompleted(task *Task) {
	 // 通过RPC，把task发送给master
	 reply := ExampleReply{}
	 call("Master.TaskCompleted", task, &reply)
 }


 func writeToLocalFile(x int, y int, kvs *[]KeyValue) string {
	 dir, _ := os.Getwd()
	 tempFile, err := ioutil.TempFile(dir, "mr-tmp-*")
	 if err != nil {
		 log.Fatal("Failed to create temp file", err)
	 }
	 enc := json.NewEncoder(tempFile)
	 for _,kv : range *kvs {
		 if err := enc.Encode(&kv); err != nil {
			 log.Fatal("Failed to write kv pair", err)
		 }
	 }
	 tempFile.Close()
	 outputName := fmt.Sprintf("mr-%d-%d", x, y)
	 os.Rename(tempFile.Name(), outputName)
	 return filepath.Join(dir, outputName)
 }

 func readFromLocalFile(files []string) *[]KeyValue {
	 kva := []KeyValue{}
	 for _, filepath := range files {
		 file, err := os.Open(filepath)
		 if err != nil {
			 log.Fatal("Failed to open file" + filepath, err)
		 }
		 dec := json.NewEncoder(file)
		 for {
			var kv KeyValue
			if err := dec.Decode(&kv); err != nil {
				break
			}
			kva = append(kva, kv)
		 }
		 file.Close()
	 }
	 return &kva
 }